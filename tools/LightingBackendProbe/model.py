"""Bounded read-only evidence parsing. No vendor activation or hardware calls."""
import hashlib
import json
import struct
import xml.etree.ElementTree as ET

MAX_PAYLOAD = 1024 * 1024
MAX_MAPPING = 128 * 1024 * 1024
MAGIC = 0x46434D4C
READ_IDS = (3, 4, 63)


def header(data, mapping_size):
    if not isinstance(mapping_size, int) or not 64 <= mapping_size <= MAX_MAPPING or len(data) < 64:
        raise ValueError('InvalidMappingSize')
    magic, version, width, height, stride, fmt, index, active, state, tick = struct.unpack_from('<9Iq', data)
    if magic != MAGIC or version != 1:
        raise ValueError('UnsupportedHeader')
    if not 0 < width <= 16384 or not 0 < height <= 16384 or fmt != 0 or active not in (0, 1):
        raise ValueError('InvalidFrameLayout')
    if stride < width * 4 or stride % 4:
        raise ValueError('InvalidStride')
    frame = stride * height
    # Python integers do not overflow; reject before any address calculation/copy.
    required = 64 + 2 * frame
    if frame > MAX_MAPPING // 2 or required > mapping_size:
        raise ValueError('BufferOutOfBounds')
    return dict(magic=magic, version=version, width=width, height=height, stride=stride,
                format=fmt, frameIndex=index, activeBuffer=active, captureState=state,
                lastClientReadTick=tick, frameBytes=frame, requiredBytes=required,
                activeOffset=64 + active * frame, bufferCount=2,
                desktopOriginX=struct.unpack_from('<i', data, 44)[0], desktopOriginY=struct.unpack_from('<i', data, 48)[0])


def frame_sample(read, mapping_size):
    """read(offset,size) returns copied bytes, never an escaped native pointer."""
    before = read(0, 64)
    h = header(before, mapping_size)
    payload = read(h['activeOffset'], h['frameBytes'])
    if len(payload) != h['frameBytes']:
        raise ValueError('TruncatedFrame')
    after = read(0, 64)
    # This is passive observation, not a handshake. A concurrent update is discarded.
    return dict(header=h, coherent=before == after, sha256=hashlib.sha256(payload).hexdigest(),
                nonzeroBytes=len(payload) - payload.count(0))


def sample_summary(samples):
    good = [s for s in samples if s['coherent']]
    return dict(sampleCount=len(samples), coherentCount=len(good),
                distinctHashes=len({s['sha256'] for s in good}),
                activeBufferTransitions=sum(a['header']['activeBuffer'] != b['header']['activeBuffer']
                    for a, b in zip(good, good[1:])),
                producerCadence='Unknown; polling gives only a lower bound, not physical update rate')


def xml_root(payload):
    if not isinstance(payload, str) or len(payload.encode('utf-8')) > MAX_PAYLOAD:
        raise ValueError('PayloadBound')
    upper = payload.upper()
    if '<!DOCTYPE' in upper or '<!ENTITY' in upper:
        raise ValueError('UnsafeXml')
    try:
        root = ET.fromstring(payload)
    except ET.ParseError as e:
        raise ValueError('MalformedXml') from e
    def walk(node, depth=0):
        if depth > 32:
            raise ValueError('XmlDepth')
        return 1 + sum(walk(c, depth + 1) for c in node)
    if walk(root) > 50000:
        raise ValueError('XmlNodeBound')
    return root


def device_descriptors(payload, source, confidence):
    if confidence not in ('RuntimeExact', 'ConfigExact', 'StrongCorrelation', 'Unknown'):
        raise ValueError('UnknownConfidence')
    root = xml_root(payload)
    nodes = root.findall('./devicelist/device')
    if not nodes:
        nodes = root.findall('./device')
    if len(nodes) > 128:
        raise ValueError('DeviceBound')
    rows = []
    for position, d in enumerate(nodes):
        fields = {c.tag: (c.text or '') for c in d if len(c) == 0}
        # Source-qualified identity; similar names never join records across sources.
        identity = d.attrib.get('key') or json.dumps(fields, sort_keys=True, ensure_ascii=False)
        rows.append(dict(id=f'{source}:{position}:{identity}', source=source, confidence=confidence,
                         vendorKey=d.attrib.get('key'), fields=fields,
                         logicalLightCount=d.findtext('./ledlist/ledcount'),
                         physicalIdentityConfidence='Unknown', accessible='NotAttempted', validated='MetadataOnly'))
    return rows


def mediator_result(records, exit_code, timed_out):
    if not isinstance(records, list) or any(not isinstance(r, dict) for r in records):
        return dict(outcome='MalformedOutput', activation=dict(state='NotAttempted', hresult=None), queries=[])
    activation = next((r for r in records if r.get('stage') == 'Activation'), None)
    if timed_out or exit_code == 124:
        outcome = 'Timeout'
    elif exit_code not in (None, 0):
        outcome = 'Crash' if (exit_code & 0xffffffff) >= 0x80000000 else 'Failed'
    elif not any(r.get('stage') == 'Completed' for r in records):
        outcome = 'TruncatedOutput'
    else:
        outcome = 'Completed'
    if activation is None and any(r.get('stage') == 'ActivationPending' for r in records):
        activation = dict(state='Timeout' if outcome == 'Timeout' else 'Failed', hresult=None,
                          execution='StartedNotReturned')
    queries = []
    for dispid in READ_IDS:
        r = next((dict(r) for r in records if r.get('stage') == 'QueryReturned' and r.get('dispid') == dispid), None)
        if r is None:
            started = any(r.get('stage') == 'QueryPending' and r.get('dispid') == dispid for r in records)
            queries.append(dict(dispid=dispid, state=('Timeout' if outcome == 'Timeout' else 'Failed') if started else 'NotAttempted',
                                execution='StartedNotReturned' if started else 'NotAttempted', payloadBytes=None, hresult=None))
            continue
        if r.get('state') == 'Succeeded':
            try:
                if r.get('variantType') != 8:
                    raise ValueError('WrongVariantType')
                xml_root(r.get('payload'))
                r['topology'] = device_descriptors(r['payload'], f'mediator-{dispid}', 'RuntimeExact')
            except ValueError as e:
                r['state'] = 'MalformedResponse'
                r['diagnostic'] = str(e)
        queries.append(r)
    return dict(outcome=outcome, activation=activation or dict(state='NotAttempted', hresult=None), queries=queries)


def wdl_descriptors(devices):
    if len(devices) > 32:
        raise ValueError('LampInterfaceBound')
    return [dict(id=d['interfaceId'], name=d['name'], source='WindowsLampArrayInterface',
                 confidence='RuntimeExact', physicalIdentityConfidence='Unknown', lampMetadataState='NotAttempted')
            for d in devices]


def choose_backend(candidates, aura_installed=None, aura_registered=None):
    # Explicit conditions, no numeric score. Read-only diagnostics do not establish a write backend.
    preferred_metadata = next((c['name'] for c in candidates if c['name'] == 'LightingServiceMediator'
                               and c.get('readOnlyValidated') is True), None)
    eligible = [c for c in candidates if c.get('readOnlyValidated') is True
                and c.get('perDeviceAddressingValidated') is True and c.get('restoreValidated') is True
                and c.get('outputProtocolValidated') is True]
    selected = eligible[0]['name'] if len(eligible) == 1 else None
    return dict(status='ALTERNATIVE_AURA_BACKEND_SELECTED' if selected else 'ALTERNATIVE_AURA_BACKEND_NOT_READY',
                selectedBackend=selected, preferredMetadataBackend=preferred_metadata,
                auraSdk=dict(installed='Unknown' if aura_installed is None else 'Detected' if aura_installed else 'Unavailable',
                             registered='Unknown' if aura_registered is None else 'Detected' if aura_registered else 'Unavailable', runtime='EnumerationUnreliable',
                             role='DiagnosticUnsupported', provenance='M2.7 historical host evidence; not rerun in M2.8'),
                gateAStatus='BlockedByAuraSdkRuntimeInstability', reviewedGateAExecutionEnabled=False,
                alternativeWriteApprovalRequired=True, executableWriteGate=False)
