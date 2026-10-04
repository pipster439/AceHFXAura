#include "Experiment.h"
#include <thread>
#include <stdexcept>
namespace gatea {
const char* Name(State state) {
    constexpr const char* names[]={"NotStarted","BaselineCaptured","ComActivated","PreEnumerationComplete",
        "SwitchModePending","SwitchModeReturned","PostEnumerationComplete","ReleasePending","ReleaseReturned",
        "RestorationCheckComplete","Completed","Failed","RecoveryRequired"};
    return names[static_cast<int>(state)];
}
Json CallResult::ToJson() const {return {{"execution",execution},{"hresult",hresult},{"exceptionCode",exceptionCode},{"reason",reason}};}
bool IsMedium(const Identity& i) {return i.valid && i.integrityRid==0x2000 && !i.elevated && i.pid>0 && i.session>0 && !i.sid.empty() && i.sid!="S-1-5-18";}
Mode ParseMode(const std::vector<std::string>& args) {return args.empty()?Mode::DryRun:args.size()==1 && args[0]=="--execute-gate-a"?Mode::Execute:Mode::Invalid;}
bool ValidTtl(int milliseconds) {return milliseconds>0 && milliseconds<=HardMaximumTtlMs;}
namespace {
using Clock=std::chrono::steady_clock;
struct Context:std::enable_shared_from_this<Context> {
    std::shared_ptr<IAuraGateAApi> api;
    std::shared_ptr<IJournal> journal;
    std::shared_ptr<StopSignal> stop;
    Review review;
    std::mutex gate;
    std::condition_variable changed;
    Result result;
    bool armed=false,done=false,releaseScheduled=false,releaseDone=false,journalFailed=false,workerFinished=false;
    Clock::time_point deadline{},releaseStarted{};
    CallResult releaseResult;
    void Record(State state,std::string event,Json details=Json::object()) {
        std::lock_guard lock(gate);
        // Recovery is absorbing: a late vendor return cannot erase the uncertain terminal result.
        bool alreadyReleasing=releaseScheduled && state>=State::SwitchModePending && state<State::ReleasePending;
        if(result.state!=State::RecoveryRequired && event!="LateSwitchModeReturned" && !alreadyReleasing) result.state=state;
        try {journal->Append({{"state",Name(result.state)},{"event",event},{"details",details},
            {"switchAttempted",result.switchAttempted},{"switchReturned",result.switchReturned},
            {"releaseAttempted",result.releaseAttempted},{"releaseReturned",result.releaseReturned},
            {"releaseArgument",review.reserve},{"hardwareRestorationVerified",nullptr}});}
        catch(...) {journalFailed=true;result.state=armed?State::RecoveryRequired:State::Failed;result.reason="JournalWriteFailed";}
    }
    void Recovery(std::string reason,bool forceTerminal=false) {
        {std::lock_guard lock(gate);result.state=State::RecoveryRequired;result.reason=reason;done=forceTerminal || !armed || releaseDone;}
        Record(State::RecoveryRequired,"RECOVERY_REQUIRED",{{"reason",reason}});changed.notify_all();
    }
    void RequestRelease(const char* cause) {
        {
            std::lock_guard lock(gate);
            if(!armed || releaseScheduled) return;
            releaseScheduled=true;releaseStarted=Clock::now();
        }
        Record(State::ReleasePending,"ReleasePending",{{"cause",cause},{"sameInstance",true},{"reviewedReserve",review.reserve}});
        // Retain the only original release-capable object. All participating vendor threads initialize MTA.
        auto self=shared_from_this();
        try {
            std::thread([self]{
                CallResult call;bool entered=false;
                try {
                    self->api->ThreadEnter();entered=true;
                    {std::lock_guard lock(self->gate);self->result.releaseAttempted=true;}
                    call=self->api->ReleaseOnce(self->review.reserve);
                }
                catch(...) {call={"Failed",nullptr,nullptr,entered?"ReleaseException":"ReleaseApartmentInitializationFailedNoVendorCall"};}
                if(entered) self->api->ThreadExit();
                {std::lock_guard lock(self->gate);self->releaseResult=call;self->releaseDone=true;self->result.releaseReturned=entered && call.exceptionCode.is_null() && call.reason!="ReleaseException";}
                self->Record(State::ReleaseReturned,"ReleaseReturned",call.ToJson());
                if(!call.Succeeded()) self->Recovery("ReleaseFailedOrException");
                self->changed.notify_all();
            }).detach();
        } catch(...) {Recovery("ReleaseThreadStartFailed",true);}
    }
    bool StopReads() {std::lock_guard lock(gate);return stop->requested || releaseScheduled || result.state==State::RecoveryRequired || (armed && Clock::now()>=deadline);}
};
struct Apartment {std::shared_ptr<IAuraGateAApi> api;explicit Apartment(std::shared_ptr<IAuraGateAApi> a):api(std::move(a)){api->ThreadEnter();}~Apartment(){api->ThreadExit();}};
struct ReleaseGuard {std::shared_ptr<Context> c;~ReleaseGuard(){c->RequestRelease("RAII ordinary exit");}};
void Work(const std::shared_ptr<Context>& c,Mode mode) {
    bool failed=false;
    try {
        Apartment apartment(c->api);ReleaseGuard releaseGuard{c};
        auto baseline=c->api->CaptureMetadata();
        c->Record(State::BaselineCaptured,"BaselineCaptured",baseline);
        auto activation=c->api->ActivateSdk2();
        if(!activation.Succeeded()) {c->Record(State::Failed,"ComActivationFailed",activation.ToJson());throw std::runtime_error("Sdk2Unavailable");}
        c->Record(State::ComActivated,"ComActivated",activation.ToJson());
        bool zero=true;
        for(auto category:Categories) {
            if(c->stop->requested) throw std::runtime_error("CancelledBeforeSwitch");
            auto snapshot=c->api->Enumerate(category,false);
            c->Record(State::ComActivated,"PreEnumeration",snapshot);
            if(snapshot.value("execution","")!="Succeeded" || !snapshot.contains("count") || !snapshot["count"].is_number_integer()) throw std::runtime_error("PreEnumerationFailed");
            if(snapshot["count"].get<int>()!=0) zero=false;
        }
        c->Record(State::PreEnumerationComplete,"PreEnumerationComplete",{{"TargetPrevalidated",false},{"allCountsZero",zero},{"collectionMayBeCached",true}});
        {std::lock_guard lock(c->gate);if(c->journalFailed) throw std::runtime_error("JournalUnavailableBeforeOwnership");}
        if(mode==Mode::DryRun) {
            c->Record(State::Completed,"DRY_RUN",{{"ownership","NotAttempted"},{"release","NotAttempted"},{"plan","SwitchMode once -> same-category reads -> ReleaseControl(0) once -> observations"}});
        } else {
            if(!zero) throw std::runtime_error("BaselineChangedReleaseInternalRgbSideEffectReviewRequired");
            if(c->stop->requested) throw std::runtime_error("CancelledBeforeSwitch");
            if(!c->api->ConsumeOneShotApproval()) throw std::runtime_error("PreviousExperimentNeedsHumanReview");
            {
                std::lock_guard lock(c->gate);c->armed=true;c->deadline=Clock::now()+std::chrono::milliseconds(OwnershipTtlMs);
            }
            c->Record(State::SwitchModePending,"SwitchModePending",{{"releaseObligationArmedBeforeCall",true},{"ownershipTtlMs",OwnershipTtlMs}});
            {
                std::lock_guard lock(c->gate);
                if(c->journalFailed) {c->armed=false;c->result.state=State::Failed;throw std::runtime_error("SwitchPendingJournalFailedNoAcquisition");}
                c->result.switchAttempted=true;
            }
            c->changed.notify_all();
            CallResult acquisition;
            try {acquisition=c->api->AcquireOnce();}
            catch(...) {acquisition={"Failed",nullptr,nullptr,"SwitchExceptionPartialTransitionUnknown"};}
            {std::lock_guard lock(c->gate);c->result.switchReturned=acquisition.exceptionCode.is_null() && acquisition.reason!="SwitchExceptionPartialTransitionUnknown";}
            // Preserve release states if watchdog already intervened; no new reads in that case.
            if(!c->StopReads()) c->Record(State::SwitchModeReturned,"SwitchModeReturned",acquisition.ToJson());
            else {
                c->Record(State::ReleasePending,"LateSwitchModeReturned",acquisition.ToJson());
                c->Recovery("SwitchReturnedAfterReleaseWasRequestedOwnershipUnknown");
            }
            if(acquisition.Succeeded()) {
                try {
                    for(auto category:Categories) {
                        if(c->StopReads()) break;
                        auto snapshot=c->api->Enumerate(category,true);
                        c->Record(State::SwitchModeReturned,"PostEnumeration",snapshot);
                        if(snapshot.value("execution","")!="Succeeded") throw std::runtime_error("PostEnumerationFailed");
                    }
                    if(!c->StopReads()) c->Record(State::PostEnumerationComplete,"PostEnumerationComplete");
                } catch(...) {failed=true;c->Record(State::SwitchModeReturned,"PostEnumerationFailed",{{"execution","Failed"}});}
            } else {
                failed=true;
                {std::lock_guard lock(c->gate);c->result.reason="SwitchModeFailedOrExceptionPartialTransitionUnknown";}
            } // Partial transition cannot be ruled out; the reviewed cleanup policy attempts release.
            c->RequestRelease("normal sequence or failure cleanup");
            {
                std::unique_lock lock(c->gate);
                c->changed.wait(lock,[&]{return c->releaseDone || c->done;});
                if(!c->releaseDone || !c->releaseResult.Succeeded() || c->result.state==State::RecoveryRequired) return;
            }
            for(auto category:Categories) {
                auto snapshot=c->api->Enumerate(category,false);
                c->Record(State::ReleaseReturned,"PostReleaseEnumeration",snapshot);
                if(snapshot.value("execution","")!="Succeeded") failed=true;
            }
            auto after=c->api->CaptureMetadata();Json compared=Json::object();
            for(auto field:{"lightingService","wdlRegistry","lampArrayInterfaces","topology","lastConfig"})
                compared[field]=baseline.contains(field) && after.contains(field)?Json(baseline[field]==after[field]):Json(nullptr);
            c->Record(State::RestorationCheckComplete,"RestorationCheckComplete",{{"metadata",after},{"metadataEqualToBaseline",compared},
                {"restoration","UnknownHumanObservationRequired"},{"VisualStateBefore",nullptr},{"VisualStateDuringOwnership",nullptr},{"VisualStateAfterRelease",nullptr}});
            c->Record(failed?State::Failed:State::Completed,"FinalStatus",{{"evidenceCollectionComplete",!failed},{"restorationProven",false},{"reviewLatchRetained",true}});
        }
    } catch(const std::exception& e) {
        failed=true;{std::lock_guard lock(c->gate);if(c->result.state!=State::RecoveryRequired) c->result.reason=e.what();}
        c->Record(State::Failed,"SequenceException",{{"execution","Failed"},{"reason",e.what()}});
    }
    catch(...) {failed=true;c->Record(State::Failed,"SequenceException",{{"execution","Failed"},{"reason","UnknownException"}});}
    {
        std::lock_guard lock(c->gate);
        if(c->armed && (!c->releaseDone || !c->releaseResult.Succeeded())) return;
        c->done=true;
    }
    c->changed.notify_all();
}
}
Experiment::Experiment(std::shared_ptr<IAuraGateAApi> api,std::shared_ptr<IJournal> journal,Identity identity,Review review,std::shared_ptr<StopSignal> stop)
    :_api(std::move(api)),_journal(std::move(journal)),_identity(std::move(identity)),_review(std::move(review)),_stop(std::move(stop)){}
Result Experiment::Run(Mode mode) {
    auto c=std::make_shared<Context>();c->api=_api;c->journal=_journal;c->stop=_stop;c->review=_review;c->result.dryRun=mode!=Mode::Execute;
    c->Record(State::NotStarted,"IdentityAndReview",{{"pid",_identity.pid},{"sid",_identity.sid},{"session",_identity.session},
        {"integrityRid",_identity.integrityRid},{"elevated",_identity.elevated},{"inJob",_identity.inJob},{"sessionActive",_identity.sessionActive},
        {"abiSha256",_review.abiHash},{"analysisSha256",_review.analysisHash},{"ReviewedReleaseValueKnown",_review.releaseValueKnown},
        {"ReviewedGateAExecutionEnabled",_review.executionEnabled},
        {"TargetPrevalidated",false},{"switchExecution","NotAttempted"},{"releaseExecution","NotAttempted"}});
    std::string rejection;
    if(mode==Mode::Invalid) rejection="ExactExecutionFlagRequiredOrNoArgumentsForDryRun";
    else if(!IsMedium(_identity)) rejection="MediumInteractiveIdentityRequired";
    else if(mode==Mode::Execute && !_identity.sessionActive) rejection="ExecutionNeedsActiveInteractiveSession";
    else if(mode==Mode::Execute && _identity.inJob) rejection="OwnerMustNotInheritLauncherJob";
    else if(mode==Mode::Execute && (!_review.releaseValueKnown || _review.reserve!=0)) rejection="BLOCKED_RELEASE_SEMANTICS_UNKNOWN";
    else if(mode==Mode::Execute && !_review.sdkMatches) rejection="ReviewedSdkRegistrationOrHashMismatch";
    else if(mode==Mode::Execute && !_review.executionEnabled) rejection="BLOCKED_VENDOR_RELEASE_INTERNAL_RGB_SIDE_EFFECT_REVIEW_REQUIRED";
    if(!rejection.empty() || c->journalFailed) {c->result.reason=rejection;c->Record(State::Failed,"ExecutionBlocked",{{"reason",rejection}});return c->result;}
    auto started=Clock::now();
    std::thread worker([c,mode]{Work(c,mode);{std::lock_guard lock(c->gate);c->workerFinished=true;}c->changed.notify_all();});
    // This observer is in the owning process, not WinUI/launcher; it can request same-instance MTA release
    // while a vendor read is hung. It cannot cancel vendor native calls or promise hardware restoration.
    std::unique_lock lock(c->gate);
    while(!c->done) {
        if(c->workerFinished && (!c->armed || c->releaseDone)) {c->done=true;break;}
        auto now=Clock::now();
        bool request=c->armed && !c->releaseScheduled && (c->stop->requested || c->journalFailed || c->result.state==State::RecoveryRequired || now>=c->deadline);
        bool releaseTimeout=c->releaseScheduled && !c->releaseDone && now-c->releaseStarted>=std::chrono::seconds(2);
        bool hardTimeout=c->armed && now>=c->deadline+std::chrono::seconds(5);
        bool initialTimeout=!c->armed && now-started>=std::chrono::seconds(30);
        if(request) {lock.unlock();c->RequestRelease(c->stop->requested?"Console stop":"Autonomous ownership TTL");lock.lock();continue;}
        if(releaseTimeout || hardTimeout || initialTimeout) {
            lock.unlock();c->Recovery(releaseTimeout?"ReleaseTimedOut":hardTimeout?"OutstandingVendorCallHardDeadline":"BaselineTimedOutNoOwnership",true);lock.lock();break;
        }
        c->changed.wait_for(lock,std::chrono::milliseconds(10));
    }
    Result result=c->result;
    result.callsOutstanding=!c->workerFinished || (!c->releaseDone && result.releaseAttempted);
    bool unsafeToJoin=result.state==State::RecoveryRequired && !c->workerFinished;
    lock.unlock();
    if(unsafeToJoin) worker.detach();
    else worker.join();
    return result;
}
}
