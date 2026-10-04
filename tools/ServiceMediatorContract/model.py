"""Pure evidence model for M2.9. This module cannot execute the proposed S1 experiment."""
from collections import Counter

IID = '{76C92A6B-B131-4DF9-9D24-CCD6014EFB5F}'
READ_IDS = (3, 4, 63)
CONFIRMED_MUTABLE = {1, 16, 60, 61, 64, 68, 69, 70, 102}


def members(library):
    if library.get('loadHresult') != 0 or library.get('typeInfoHresult') != 0:
        raise ValueError('TypeLibraryUnavailable')
    matches = [t for t in library.get('types', []) if t.get('guid') == IID]
    if len(matches) != 1 or matches[0].get('typeAttrHresult') != 0:
        raise ValueError('InterfaceAmbiguous')
    rows = [f for f in matches[0]['functions'] if 0 < f['dispid'] < 1000]
    if len({f['dispid'] for f in rows}) != len(rows):
        raise ValueError('DuplicateMember')
    for f in rows:
        if f.get('namesHresult') != 0 or len(f['parameters']) != f['parameterCount']:
            raise ValueError('IncompleteSignature')
        if [p['order'] for p in f['parameters']] != list(range(f['parameterCount'])):
            raise ValueError('ParameterOrder')
    return rows


def type_text(t):
    vt = t['vt']
    if vt in (26, 27):
        return ('PTR' if vt == 26 else 'SAFEARRAY') + '(' + type_text(t['element']) + ')'
    return {3: 'I4', 8: 'BSTR', 19: 'UI4', 22: 'INT', 24: 'VOID', 25: 'HRESULT'}.get(vt, f'VT_{vt}')


def classify(member):
    """A getter-shaped name alone never constitutes mutability evidence."""
    n = member['dispid']
    if n in READ_IDS or n == 2:
        return 'READ_ONLY_CONFIRMED'
    if n in CONFIRMED_MUTABLE or member['invokeKind'] in (4, 8):
        return 'STATE_CHANGING_CONFIRMED'
    if n in (11, 53, 54, 62, 67, 101, 104, 105, 106, 107, 109, 110, 111):
        return 'LIKELY_STATE_CHANGING'
    return 'UNKNOWN'


def map_devices(topology, status_root, cap_root):
    """Preserve record/index identity; exact family keys do not become unique write addresses."""
    families = Counter(d['fields'].get('lightingname') for d in topology)
    sections = {d.get('key'): d for d in status_root.findall('./device')}
    connected = {d.get('key'): d.text for d in status_root.findall('./connecteddevice')}
    cap_keys = {d.get('key') for d in cap_root.findall('./device')}
    rows = []
    for d in topology:
        fields = d['fields']; family = fields.get('lightingname')
        refs = []
        for key, section in sections.items():
            led_keys = sorted({l.get('key') for l in section.findall('.//led') if l.findtext('DeviceName') == family})
            if led_keys:
                refs.append(dict(sectionKey=key, profileLedKeys=led_keys, capabilitySectionPresent=key in cap_keys))
        rows.append(dict(runtimeRecord=d['id'], fields=fields, vendorLightingFamily=family,
            connectedFamilyKey=family if family in connected else None,
            connectedFamilyCode=connected.get(family), statusReferences=refs,
            familyMultiplicity=families[family], uniqueWriteAddress=None,
            matrixSelectorCandidate=dict(typeName=fields.get('type'), modelName=fields.get('model')),
            selectorUsedForThisRecord='NotValidated', vendorIndexPassedToMatrixControl=False,
            addressingApiEvidence='Profile XML section/led keys; matrix type/model pair has no index argument',
            confidence='STRONG_INFERENCE' if refs else 'UNKNOWN',
            reason='Family/section association is not a device-specific transient write contract'))
    return rows


def matrix_payload_bounds(contract, lower_bound, element_count, element_vt):
    """Offline candidate validation only. No payload is sent anywhere."""
    if element_vt != 19 or lower_bound != 0 or type(element_count) is not int or not 1 <= element_count <= 4096:
        return dict(accepted=False, reason='SoftwareShapeBound')
    # Static server has device-specific index arithmetic and unchecked accesses. Shape is insufficient.
    if contract.get('elementSemantics') != 'CONFIRMED_STATIC' or contract.get('exactLength') != element_count:
        return dict(accepted=False, reason='PayloadContractUnknown')
    return dict(accepted=True, bytes=element_count * 4, executionAuthorized=False)


def select_target(addresses, restore):
    rejected = []
    for row in addresses:
        reasons = []
        if not row.get('uniqueWriteAddress'): reasons.append('NoUniqueWriteAddress')
        if not row.get('physicalZoneConfirmed'): reasons.append('PhysicalZoneUnknown')
        if row.get('fields', {}).get('lightingname') == 'AddressableStrip': reasons.append('AttachedLengthUnknown')
        if row.get('fields', {}).get('lightingname') == 'WDL_Keyboard': reasons.append('ExistingKeyboardPathPreserved')
        if restore.get('deterministicNormalRestore') is not True: reasons.append('RestoreContractIncomplete')
        rejected.append(dict(runtimeRecord=row['runtimeRecord'], reasons=reasons))
    return dict(selectedTarget=None, rejected=rejected)


def readiness(evidence):
    requirements = ('uniqueTargetAddress', 'physicalZone', 'writePayload', 'ownershipSequence',
                    'deterministicRestore', 'releaseSequence', 'boundedSameProcessRestore')
    missing = [name for name in requirements if evidence.get(name) is not True]
    return dict(status='SERVICE_MEDIATOR_S1_BLOCKED' if missing else 'SERVICE_MEDIATOR_S1_READY_FOR_CANDIDATE_PREPARATION',
                missing=missing, candidateExecutable=False, executionAuthorized=False,
                reviewedGateAExecutionEnabled=False, auraSdkRuntime='EnumerationUnreliable',
                gateA='BlockedByAuraSdkRuntimeInstability', lmcapRole='DesktopCaptureInput')


def execution_states(registration, records, result):
    activated = result.get('activation', {}).get('state')
    queries = []
    for n in READ_IDS:
        pending = any(r.get('stage') == 'QueryPending' and r.get('dispid') == n for r in records)
        q = next((q for q in result.get('queries', []) if q.get('dispid') == n), {})
        queries.append(dict(dispid=n, invocationStarted=pending,
            invocationSucceeded=q.get('state') == 'Succeeded' if pending else None,
            payloadValid=q.get('state') == 'Succeeded' and q.get('variantType') == 8 if pending else None,
            hresult=q.get('hresult'), interpretation='MetadataOnly'))
    return dict(registered=registration, activated=activated == 'Succeeded' if activated in ('Succeeded', 'Failed') else None,
                queries=queries)
