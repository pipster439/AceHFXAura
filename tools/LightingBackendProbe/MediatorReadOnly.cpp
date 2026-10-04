#include "MediatorReadOnly.h"
#include <shlobj.h>
#include <chrono>
#include <iostream>
#include <array>
using Json=nlohmann::json;
extern void Emit(const Json&);
extern std::string Utf8(const std::wstring&);
namespace mediator {
// TypeLib FUNC, no input arguments, BSTR retval. These are the complete allowlist.
static constexpr std::array<DISPID,3> ReadIds={3,4,63};
static constexpr std::array<const wchar_t*,3> ReadNames={L"get_QueryAllDeviceStatus",L"get_QueryAllDeviceCap",L"get_QueryAllDevice"};
std::wstring LibraryPath(){
 PWSTR p=nullptr;HRESULT h=SHGetKnownFolderPath(FOLDERID_ProgramFilesX86,0,nullptr,&p);
 if(FAILED(h))throw std::runtime_error("ProgramFilesX86 unavailable");
 std::wstring path(p);CoTaskMemFree(p);return path+L"\\LightingService\\LightingService.exe";
}
static std::string GuidText(REFGUID id){wchar_t s[40]{};StringFromGUID2(id,s,40);return Utf8(s);}
static Json TypeDescription(ITypeInfo* info,const TYPEDESC& type,int depth=0){
 Json r={{"vt",type.vt}};if(depth>8){r["error"]="TypeRecursionBound";return r;}
 if(type.vt==VT_PTR||type.vt==VT_SAFEARRAY){
  if(type.lptdesc)r["element"]=TypeDescription(info,*type.lptdesc,depth+1);
 }else if(type.vt==VT_CARRAY&&type.lpadesc){
  r["element"]=TypeDescription(info,type.lpadesc->tdescElem,depth+1);r["dimensions"]=type.lpadesc->cDims;
 }else if(type.vt==VT_USERDEFINED){
  ITypeInfo* ref=nullptr;HRESULT h=info->GetRefTypeInfo(type.hreftype,&ref);r["referenceHresult"]=static_cast<long>(h);
  if(SUCCEEDED(h)){BSTR name=nullptr;ref->GetDocumentation(MEMBERID_NIL,&name,nullptr,nullptr,nullptr);
   r["name"]=name?Utf8(std::wstring(name,SysStringLen(name))):"";SysFreeString(name);TYPEATTR* a=nullptr;
   if(SUCCEEDED(ref->GetTypeAttr(&a))){r["guid"]=GuidText(a->guid);ref->ReleaseTypeAttr(a);}ref->Release();}
 }return r;
}
static Json InterfaceMetadata(ITypeInfo* info){
 Json r={{"functions",Json::array()},{"inheritedInterfaces",Json::array()}};TYPEATTR* attr=nullptr;
 HRESULT h=info->GetTypeAttr(&attr);r["typeAttrHresult"]=static_cast<long>(h);if(FAILED(h))return r;
 BSTR name=nullptr;info->GetDocumentation(MEMBERID_NIL,&name,nullptr,nullptr,nullptr);
 r["name"]=name?Utf8(std::wstring(name,SysStringLen(name))):"";SysFreeString(name);
 r["guid"]=GuidText(attr->guid);r["typeKind"]=attr->typekind;r["typeFlags"]=attr->wTypeFlags;
 for(UINT i=0;i<attr->cFuncs;++i){FUNCDESC* f=nullptr;h=info->GetFuncDesc(i,&f);if(FAILED(h))throw std::runtime_error("GetFuncDesc failed");
  std::vector<BSTR> names(static_cast<size_t>(f->cParams)+1,nullptr);UINT count=0;
  h=info->GetNames(f->memid,names.data(),static_cast<UINT>(names.size()),&count);
  Json params=Json::array();for(int j=0;j<f->cParams;++j){
   const auto& p=f->lprgelemdescParam[j];params.push_back({{"order",j},{"name",static_cast<UINT>(j+1)<count?Utf8(std::wstring(names[j+1],SysStringLen(names[j+1]))):""},
    {"vt",p.tdesc.vt},{"flags",p.paramdesc.wParamFlags},{"type",TypeDescription(info,p.tdesc)}});
  }
  r["functions"].push_back({{"name",count?Utf8(std::wstring(names[0],SysStringLen(names[0]))):""},{"namesHresult",static_cast<long>(h)},
   {"dispid",f->memid},{"invokeKind",f->invkind},{"memberKind",f->invkind==INVOKE_PROPERTYGET?"PROPERTYGET":f->invkind==INVOKE_PROPERTYPUT?"PROPERTYPUT":f->invkind==INVOKE_PROPERTYPUTREF?"PROPERTYPUTREF":"FUNC"},
   {"functionKind",f->funckind},{"callConvention",f->callconv},{"parameterCount",f->cParams},{"optionalParameterCount",f->cParamsOpt},
   {"vtableByteOffset",f->oVft},{"returnVt",f->elemdescFunc.tdesc.vt},{"returnType",TypeDescription(info,f->elemdescFunc.tdesc)},{"parameters",params}});
  for(BSTR n:names)SysFreeString(n);info->ReleaseFuncDesc(f);
 }
 for(UINT i=0;i<attr->cImplTypes;++i){HREFTYPE id=0;ITypeInfo* ref=nullptr;int flags=0;
  h=info->GetRefTypeOfImplType(i,&id);info->GetImplTypeFlags(i,&flags);
  if(SUCCEEDED(h))h=info->GetRefTypeInfo(id,&ref);
  Json base={{"index",i},{"flags",flags},{"hresult",static_cast<long>(h)}};
  if(SUCCEEDED(h)){TYPEATTR* a=nullptr;BSTR n=nullptr;ref->GetDocumentation(MEMBERID_NIL,&n,nullptr,nullptr,nullptr);
   base["name"]=n?Utf8(std::wstring(n,SysStringLen(n))):"";SysFreeString(n);
   if(SUCCEEDED(ref->GetTypeAttr(&a))){base["guid"]=GuidText(a->guid);ref->ReleaseTypeAttr(a);}ref->Release();}
  r["inheritedInterfaces"].push_back(base);
 }
 info->ReleaseTypeAttr(attr);return r;
}
Json LibraryMetadata(){
 ITypeLib* lib=nullptr;HRESULT h=LoadTypeLibEx(LibraryPath().c_str(),REGKIND_NONE,&lib);
 Json result={{"loadHresult",static_cast<long>(h)},{"path",Utf8(LibraryPath())},{"functions",Json::array()},{"types",Json::array()},{"extractionApi","ITypeLib/ITypeInfo; REGKIND_NONE; metadata only"}};
 if(FAILED(h))return result;
 TLIBATTR* la=nullptr;if(SUCCEEDED(lib->GetLibAttr(&la))){wchar_t guid[40]{};StringFromGUID2(la->guid,guid,40);result["libraryGuid"]=Utf8(guid);result["major"]=la->wMajorVerNum;result["minor"]=la->wMinorVerNum;lib->ReleaseTLibAttr(la);}
 for(UINT i=0;i<lib->GetTypeInfoCount();++i){ITypeInfo* t=nullptr;h=lib->GetTypeInfo(i,&t);if(FAILED(h))throw std::runtime_error("GetTypeInfo failed");
  auto entry=InterfaceMetadata(t);t->Release();result["types"].push_back(entry);
 }
 ITypeInfo* info=nullptr;h=lib->GetTypeInfoOfGuid(InterfaceId,&info);result["typeInfoHresult"]=static_cast<long>(h);
 if(SUCCEEDED(h)){
  TYPEATTR* attr=nullptr;if(SUCCEEDED(info->GetTypeAttr(&attr))){
   for(UINT i=0;i<attr->cFuncs;++i){FUNCDESC* f=nullptr;if(FAILED(info->GetFuncDesc(i,&f)))continue;
    BSTR name=nullptr;info->GetDocumentation(f->memid,&name,nullptr,nullptr,nullptr);
    Json params=Json::array();for(int j=0;j<f->cParams;++j)params.push_back({{"vt",f->lprgelemdescParam[j].tdesc.vt},{"flags",f->lprgelemdescParam[j].paramdesc.wParamFlags}});
    result["functions"].push_back({{"name",name?Utf8(std::wstring(name,SysStringLen(name))):""},{"dispid",f->memid},{"invokeKind",f->invkind},{"parameterCount",f->cParams},{"vtableByteOffset",f->oVft},{"returnVt",f->elemdescFunc.tdesc.vt},{"parameters",params}});
    SysFreeString(name);info->ReleaseFuncDesc(f);
   }
   info->ReleaseTypeAttr(attr);
  }info->Release();
 }lib->Release();return result;
}
bool VerifyReadOnlyContract(){
 auto m=LibraryMetadata();if(m.value("libraryGuid","")!="{61E8C91A-C37E-4831-87C0-2FBD8C5A85D5}"||m.value("major",0)!=1)return false;
 for(size_t i=0;i<ReadIds.size();++i){bool found=false;for(const auto& f:m["functions"]){
  if(f["dispid"]==ReadIds[i]&&f["name"]==Utf8(ReadNames[i])&&f["invokeKind"]==INVOKE_FUNC&&f["parameterCount"]==0&&f["returnVt"]==VT_BSTR)found=true;
 }if(!found)return false;}return true;
}
void QueryMetadata(IDispatch* service){
 // No caller-supplied method, arguments, CLSID or DLL. Only the immutable three queries above.
 for(size_t i=0;i<ReadIds.size();++i){
  Emit({{"stage","QueryPending"},{"dispid",ReadIds[i]},{"name",Utf8(ReadNames[i])}});
  VARIANT value;VariantInit(&value);EXCEPINFO exception{};UINT argument=0;DISPPARAMS args{};
  auto start=std::chrono::steady_clock::now();
  HRESULT h=service->Invoke(ReadIds[i],IID_NULL,LOCALE_INVARIANT,DISPATCH_METHOD,&args,&value,&exception,&argument);
  Json result={{"stage","QueryReturned"},{"dispid",ReadIds[i]},{"name",Utf8(ReadNames[i])},{"hresult",static_cast<long>(h)},{"latencyMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()},{"variantType",value.vt},{"payloadBytes",nullptr},{"payload",nullptr},{"state",FAILED(h)?"Failed":"Succeeded"}};
  if(SUCCEEDED(h)){
   if(value.vt!=VT_BSTR)result["state"]="MalformedResponse";
   else{UINT length=value.bstrVal?SysStringLen(value.bstrVal):0;result["payloadBytes"]=static_cast<uint64_t>(length)*2;
    if(length>512*1024)result["state"]="ResponseTooLarge";
    else result["payload"]=value.bstrVal?Utf8(std::wstring(value.bstrVal,length)):"";
   }
  }
  VariantClear(&value);SysFreeString(exception.bstrSource);SysFreeString(exception.bstrDescription);SysFreeString(exception.bstrHelpFile);Emit(result);
  if(result["state"]!="Succeeded")return;
 }
}
}
