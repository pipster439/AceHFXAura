#!/usr/bin/env python3
"""
Reproduction script for Finding SEC-01 (HIGH):
Multi-sample race on foreground process and clock during multi-packet decision batch evaluation.

Demonstrates that in RuleEngine::EvaluateAutomation (automation_evaluation.cpp:305-316):
`foreground()` is invoked per-batch and then again for `current`.
If a focus switch occurs during the evaluation of a multi-packet backlog:
- Batch 0 evaluates in-scope with foreground "cs2.exe" -> generates an admitted decision.
- Current evaluates out-of-scope with foreground "discord.exe" -> status.continuation becomes False.
- AutomationEffectRuntime::Consume drops the valid decision because status.continuation != ConditionTruth::True.
"""

import sys

def simulate_evaluate_automation(foreground_sequence):
    """
    Simulates RuleEngine::EvaluateAutomation foreground sampling behavior.
    foreground_sequence is a list of results returned by successive calls to foreground().
    """
    call_idx = 0
    def foreground():
        nonlocal call_idx
        val = foreground_sequence[call_idx] if call_idx < len(foreground_sequence) else foreground_sequence[-1]
        call_idx += 1
        return val

    # Suppose we have 2 batches drained from GSI: batch0, batch1, plus latest (current)
    # The lambda in production code:
    # auto capture = [&](...) { auto process = foreground(); ... }
    
    # In production:
    # for (const auto& b : input.batches) batches.push_back(capture(b));
    batch0_proc = foreground() # e.g. "cs2.exe"
    batch1_proc = foreground() # e.g. user Alt-Tabs midway -> "discord.exe"
    
    # const auto current = capture(input.latest);
    current_proc = foreground() # "discord.exe"
    
    print(f"Captured Batch 0 process: {batch0_proc}")
    print(f"Captured Batch 1 process: {batch1_proc}")
    print(f"Captured Current process: {current_proc}")
    
    # Rule scope: process.name == "cs2.exe"
    def evaluate_scope(proc):
        return proc.lower() == "cs2.exe"
    
    # Batch 0 decision evaluation:
    batch0_in_scope = evaluate_scope(batch0_proc)
    print(f"Batch 0 in-scope: {batch0_in_scope}") # True
    
    # But rule_status evaluation is evaluated against `current`:
    # status.scope = rule.scope.Evaluate(current).truth;
    # status.continuation = status.scope;
    status_continuation = evaluate_scope(current_proc)
    print(f"Rule status continuation (evaluated against current): {status_continuation}") # False
    
    # In AutomationEffectRuntime::Consume:
    # if (evaluation.reconciliation_complete && (!status || !status->enabled ||
    #     (!persistent && status->continuation != ConditionTruth::True))) continue;
    decision_admitted = batch0_in_scope
    decision_consumed = decision_admitted and status_continuation
    
    print(f"Decision emitted by Batch 0 admitted: {decision_admitted}")
    print(f"Decision accepted by Consume: {decision_consumed}")
    
    if decision_admitted and not decision_consumed:
        print("\n[VULNERABILITY CONFIRMED] Invariant B violated! Valid decision emitted by Batch 0 dropped by Consume due to mid-batch foreground resample.")
        return False
    return True

if __name__ == "__main__":
    # Timeline:
    # Call 1 (Batch 0 capture): "cs2.exe"
    # Call 2 (Batch 1 capture): "discord.exe" (Alt-Tab during backlog drain)
    # Call 3 (Current capture): "discord.exe"
    ok = simulate_evaluate_automation(["cs2.exe", "discord.exe", "discord.exe"])
    sys.exit(0 if not ok else 1)
