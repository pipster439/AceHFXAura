// Minimum verified Phase 2.1 TypeLib-derived declarations. No calls to setters/ownership methods.
// Evidence: docs/research/aura-typelib-raw.json (LIBID F1AA5209-5217-4B82-BA7E-A68198999AFA),
// asus-generated-interop-audit.md and src/research/generated/aura/AuraSdk.h, preserved in
// audit_artifacts/backup_pre_rollback_20260929_2325. Exact native slot order retained, including prohibited slots.
#pragma once
#include <windows.h>
#include <oaidl.h>
DEFINE_GUID(CLSID_AuraSdk, 
    0x05921124, 0x5057, 0x483e, 0xa0, 0x37, 0xe9, 0x49, 0x7b, 0x52, 0x35, 0x90);
struct IAuraRgbLight;
struct IAuraMbLight;
struct IAuraRgbLightCollection;
struct IAuraSyncDevice;
struct IAuraSyncDeviceCollection;
struct IAuraSdk;
struct IAuraSdk2;
struct IAuraSdk3;
MIDL_INTERFACE("9AF6260E-4311-417D-B3EF-B85A34CF3244")
IAuraRgbLight : public IDispatch
{
public:
    virtual /* [id(1)][propget] */ HRESULT STDMETHODCALLTYPE get_Red(
        /* [out][retval] */ BYTE *pVal) = 0;

    virtual /* [id(1)][propput] */ HRESULT STDMETHODCALLTYPE put_Red(
        /* [in] */ BYTE pVal) = 0;

    virtual /* [id(2)][propget] */ HRESULT STDMETHODCALLTYPE get_Green(
        /* [out][retval] */ BYTE *pVal) = 0;

    virtual /* [id(2)][propput] */ HRESULT STDMETHODCALLTYPE put_Green(
        /* [in] */ BYTE pVal) = 0;

    virtual /* [id(3)][propget] */ HRESULT STDMETHODCALLTYPE get_Blue(
        /* [out][retval] */ BYTE *pVal) = 0;

    virtual /* [id(3)][propput] */ HRESULT STDMETHODCALLTYPE put_Blue(
        /* [in] */ BYTE pVal) = 0;

    virtual /* [id(4)][propget] */ HRESULT STDMETHODCALLTYPE get_Name(
        /* [out][retval] */ BSTR *pVal) = 0;

    virtual /* [id(5)][propget] */ HRESULT STDMETHODCALLTYPE get_Color(
        /* [out][retval] */ ULONG *pVal) = 0;

    virtual /* [id(5)][propput] */ HRESULT STDMETHODCALLTYPE put_Color(
        /* [in] */ ULONG pVal) = 0;
};

MIDL_INTERFACE("EBC2ECDC-1B1B-4613-8972-19C0ECEEB5BF")
IAuraMbLight : public IAuraRgbLight
{
public:
    virtual /* [id(6)][propget] */ HRESULT STDMETHODCALLTYPE get_Speed(
        /* [out][retval] */ ULONG *pVal) = 0;

    virtual /* [id(6)][propput] */ HRESULT STDMETHODCALLTYPE put_Speed(
        /* [in] */ ULONG pVal) = 0;

    virtual /* [id(7)][propget] */ HRESULT STDMETHODCALLTYPE get_Direction(
        /* [out][retval] */ ULONG *pVal) = 0;

    virtual /* [id(7)][propput] */ HRESULT STDMETHODCALLTYPE put_Direction(
        /* [in] */ ULONG pVal) = 0;

    virtual /* [id(8)][propget] */ HRESULT STDMETHODCALLTYPE get_LocationId(
        /* [out][retval] */ ULONG *pVal) = 0;
};

MIDL_INTERFACE("003065BC-B562-454E-ADCB-A9B70042D486")
IAuraRgbLightCollection : public IDispatch
{
public:
    virtual /* [id(DISPID_NEWENUM)][propget] */ HRESULT STDMETHODCALLTYPE get__NewEnum(
        /* [out][retval] */ IUnknown **pVal) = 0;

    virtual /* [id(2)][propget] */ HRESULT STDMETHODCALLTYPE get_Count(
        /* [out][retval] */ INT *pVal) = 0;

    virtual /* [id(DISPID_VALUE)][propget] */ HRESULT STDMETHODCALLTYPE get_Item(
        /* [in] */ INT index,
        /* [out][retval] */ IAuraRgbLight **pVal) = 0;
};

MIDL_INTERFACE("6A30D789-F5DA-4F26-BF09-6DFB9DEDF91E")
IAuraSyncDevice : public IDispatch
{
public:
    virtual /* [id(7)][propget] */ HRESULT STDMETHODCALLTYPE get_Lights(
        /* [out][retval] */ IAuraRgbLightCollection **value) = 0;

    virtual /* [id(8)][propget] */ HRESULT STDMETHODCALLTYPE get_Type(
        /* [out][retval] */ ULONG *value) = 0;

    virtual /* [id(9)][propget] */ HRESULT STDMETHODCALLTYPE get_Name(
        /* [out][retval] */ BSTR *value) = 0;

    virtual /* [id(13)][propget] */ HRESULT STDMETHODCALLTYPE get_Width(
        /* [out][retval] */ ULONG *value) = 0;

    virtual /* [id(14)][propget] */ HRESULT STDMETHODCALLTYPE get_Height(
        /* [out][retval] */ ULONG *value) = 0;

    virtual /* [id(1)] */ HRESULT STDMETHODCALLTYPE Apply(void) = 0;
};

MIDL_INTERFACE("87FC56AB-99CA-4FD3-B561-2EEDD719DA57")
IAuraSyncDeviceCollection : public IDispatch
{
public:
    virtual /* [id(DISPID_NEWENUM)][propget] */ HRESULT STDMETHODCALLTYPE get__NewEnum(
        /* [out][retval] */ IUnknown **pVal) = 0;

    virtual /* [id(2)][propget] */ HRESULT STDMETHODCALLTYPE get_Count(
        /* [out][retval] */ INT *pVal) = 0;

    virtual /* [id(DISPID_VALUE)][propget] */ HRESULT STDMETHODCALLTYPE get_Item(
        /* [in] */ INT index,
        /* [out][retval] */ IAuraSyncDevice **pVal) = 0;
};

MIDL_INTERFACE("3CED2297-27BD-492C-9934-D1D153B0FAC1")
IAuraSdk : public IDispatch
{
public:
    virtual /* [id(1)] */ HRESULT STDMETHODCALLTYPE Enumerate(
        /* [in] */ ULONG devType,
        /* [out][retval] */ IAuraSyncDeviceCollection **devices) = 0;

    virtual /* [id(2)] */ HRESULT STDMETHODCALLTYPE SwitchMode(void) = 0;
};

MIDL_INTERFACE("EE69DBAE-33FF-4E45-B378-01797A59852D")
IAuraSdk2 : public IAuraSdk
{
public:
    virtual /* [id(3)] */ HRESULT STDMETHODCALLTYPE ReleaseControl(
        /* [in] */ ULONG reserve) = 0;
};

MIDL_INTERFACE("B9A57A8A-7CAF-46D8-960B-2328330FE8C6")
IAuraSdk3 : public IAuraSdk2
{
public:
    virtual /* [id(4)] */ HRESULT STDMETHODCALLTYPE RequireTokenByType(
        /* [in] */ VARIANT *types,
        /* [in] */ INT numberOftypes) = 0;

    virtual /* [id(5)] */ HRESULT STDMETHODCALLTYPE RequireDeviceControlState(
        /* [in] */ ULONG Type,
        /* [out][retval] */ INT *is_controlled) = 0;
};
