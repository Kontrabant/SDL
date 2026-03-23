/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "../../events/SDL_notificationevents_c.h"

#include <Windows.h>
#include <Windows.ui.notifications.h>
#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

DWORD dwMainThreadId = 0;

typedef interface INotificationActivationCallback INotificationActivationCallback;

typedef struct NOTIFICATION_USER_INPUT_DATA
{
    LPCWSTR Key;
    LPCWSTR Value;
} NOTIFICATION_USER_INPUT_DATA;

DEFINE_GUID(IID_INotificationActivationCallback, 0x53e31837, 0x6600, 0x4a81, 0x93, 0x95, 0x75, 0xcf, 0xfe, 0x74, 0x6f, 0x94);

typedef struct INotificationActivationCallbackVtbl
{
    BEGIN_INTERFACE

    /*** IUnknown methods ***/
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(
        INotificationActivationCallback *This,
        REFIID riid,
        void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)(
        INotificationActivationCallback *This);

    ULONG(STDMETHODCALLTYPE *Release)(
        INotificationActivationCallback *This);

    /*** INotificationActivationCallback methods ***/
    HRESULT(STDMETHODCALLTYPE *Activate)(
        INotificationActivationCallback *This,
        LPCWSTR appUserModelId,
        LPCWSTR invokedArgs,
        const NOTIFICATION_USER_INPUT_DATA *data,
        ULONG count);

    END_INTERFACE
} INotificationActivationCallbackVtbl;

interface INotificationActivationCallback
{
    CONST_VTBL INotificationActivationCallbackVtbl *lpVtbl;
};

// The registry key base needed to register the app instance.
#define REG_KEY_BASE L"SOFTWARE\\Classes\\AppUserModelId\\"

/*
 * The XML that describes the notification that will be shown. Of course, this can be built at runtime,
 * and more can be done with it, but for this basic example, this will suffice.
 */
const wchar_t wszBannerText[] =
    L"<toast scenario=\"default\" "
    L"activationType=\"foreground\" launch=\"action=mainContent\" duration=\"short\">\r\n"
    L"	<visual>\r\n"
    L"		<binding template=\"ToastGeneric\">\r\n"
    L"                  <image id=\"1\" placement=\"appLogoOverride\" src=\"file:///C:\\Users\\franz\\AppData\\Local\\Temp\\SDL_temp.png\"></image>\r\n"
    L"			<text><![CDATA[This is a header]]></text>\r\n"
    L"			<text><![CDATA[This is the body and should be long so multiple lines are involved ok this is longer heeeey]]></text>\r\n"
    L"		</binding>\r\n"
    L"	</visual>\r\n"
    L"  <actions>\r\n"
    L"	  <action content=\"Button 1\" activationType=\"foreground\" arguments=\"notification_id=1,action=button_1\"/>\r\n"
    L"	  <action content=\"Button 2\" activationType=\"foreground\" arguments=\"notification_id=1,action=button_2\"/>\r\n"
    L"  </actions>\r\n"
    // L"	<audio src=\"ms-winsoundevent:Notification.Default\" loop=\"false\" silent=\"false\"/>\r\n"
    L"</toast>\r\n";

/*
 * IIDs of other interfaces we use throughout this example.
 */
DEFINE_GUID(IID_IToastNotificationManagerStatics,
            0x50ac103f, 0xd235, 0x4598, 0xbb, 0xef, 0x98, 0xfe, 0x4d, 0x1a, 0x3a, 0xd4);

DEFINE_GUID(IID_IToastNotificationFactory,
            0x04124b20, 0x82c6, 0x4229, 0xb1, 0x09, 0xfd, 0x9e, 0xd4, 0x66, 0x2b, 0x53);

DEFINE_GUID(IID_IXmlDocument,
            0xf7f3a506, 0x1e87, 0x42d6, 0xbc, 0xfb, 0xb8, 0xc8, 0x09, 0xfa, 0x54, 0x94);

DEFINE_GUID(IID_IXmlDocumentIO,
            0x6cd0e74e, 0xee65, 0x4489, 0x9e, 0xbf, 0xca, 0x43, 0xe8, 0x7b, 0xa6, 0x37);

/*
 * All the objects we allocate in this example (our class factory and our INotificationActivationCallback
 * implementation) have this memory layout, and all inherit from IUnknown.
 */
#pragma region "IGeneric : IUnknown implementation"
typedef struct Impl_IGeneric
{
    IUnknownVtbl *lpVtbl;
    LONG64 dwRefCount;
} Impl_IGeneric;

static ULONG STDMETHODCALLTYPE Impl_IGeneric_AddRef(Impl_IGeneric *_this)
{
    return InterlockedIncrement64(&(_this->dwRefCount));
}

static ULONG STDMETHODCALLTYPE Impl_IGeneric_Release(Impl_IGeneric *_this)
{
    LONG64 dwNewRefCount = InterlockedDecrement64(&(_this->dwRefCount));
    if (!dwNewRefCount) {
        SDL_free(_this);
    }
    return dwNewRefCount;
}
#pragma endregion

/*
 * Our INotificationActivationCallback implementation.
 */
#pragma region "INotificationActivationCallback : IGeneric implementation"
static HRESULT STDMETHODCALLTYPE Impl_INotificationActivationCallback_QueryInterface(Impl_IGeneric *_this, REFIID riid, void **ppvObject)
{
    if (!IsEqualIID(riid, &IID_INotificationActivationCallback) && !IsEqualIID(riid, &IID_IUnknown)) {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
    *ppvObject = _this;
    _this->lpVtbl->AddRef((IUnknown *)_this);
    return S_OK;
}

/*
 * This is where the magic happens when someone interacts with our notification: this method will be called
 * (on another thread !!!).
 */
static HRESULT STDMETHODCALLTYPE Impl_INotificationActivationCallback_Activate(INotificationActivationCallback *_this, LPCWSTR appUserModelId, LPCWSTR invokedArgs, const NOTIFICATION_USER_INPUT_DATA *data, ULONG count)
{
    SDL_NotificationID id = 0;
    WCHAR action[256] = L"";
    swscanf(invokedArgs, L"notification_id=%" SDL_PRIu32 ",action=%ls", &id, action);

    int len = WideCharToMultiByte(CP_UTF8, 0, action, -1, NULL, 0, NULL, NULL);
    char *utf8_action = SDL_malloc(len * sizeof(char));
    WideCharToMultiByte(CP_UTF8, 0, action, -1, utf8_action, len, NULL, NULL);

    SDL_SendNotificationAction(id, utf8_action);
    SDL_free(utf8_action);

    return S_OK;
}

static const INotificationActivationCallbackVtbl Impl_INotificationActivationCallback_Vtbl = {
    .QueryInterface = (void *)Impl_INotificationActivationCallback_QueryInterface,
    .AddRef = (void *)Impl_IGeneric_AddRef,
    .Release = (void *)Impl_IGeneric_Release,
    .Activate = Impl_INotificationActivationCallback_Activate
};
#pragma endregion

/*
 * Our IClassFactory implementation.
 */
#pragma region "IClassFactory : IGeneric implementation"
static HRESULT STDMETHODCALLTYPE Impl_IClassFactory_QueryInterface(Impl_IGeneric *_this, REFIID riid, void **ppvObject)
{
    if (!IsEqualIID(riid, &IID_IClassFactory) && !IsEqualIID(riid, &IID_IUnknown)) {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
    *ppvObject = _this;
    _this->lpVtbl->AddRef((IUnknown *)_this);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Impl_IClassFactory_LockServer(IClassFactory *_this, BOOL flock)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Impl_IClassFactory_CreateInstance(IClassFactory *_this, IUnknown *punkOuter, REFIID vTableGuid, void **ppv)
{
    HRESULT hr = E_NOINTERFACE;
    Impl_IGeneric *thisobj = NULL;
    *ppv = 0;

    if (punkOuter) {
        hr = CLASS_E_NOAGGREGATION;
    } else {
        BOOL bOk = FALSE;
        thisobj = SDL_malloc(sizeof(Impl_IGeneric));
        if (!thisobj) {
            hr = E_OUTOFMEMORY;
        } else {
            thisobj->lpVtbl = (IUnknownVtbl *)&Impl_INotificationActivationCallback_Vtbl;
            bOk = TRUE;
        }
        if (bOk) {
            thisobj->dwRefCount = 1;
            hr = thisobj->lpVtbl->QueryInterface((IUnknown *)thisobj, vTableGuid, ppv);
            thisobj->lpVtbl->Release((IUnknown *)thisobj);
        } else {
            return hr;
        }
    }

    return hr;
}

static const IClassFactoryVtbl Impl_IClassFactory_Vtbl = {
    .QueryInterface = (void *)Impl_IClassFactory_QueryInterface,
    .AddRef = (void *)Impl_IGeneric_AddRef,
    .Release = (void *)Impl_IGeneric_Release,
    .LockServer = Impl_IClassFactory_LockServer,
    .CreateInstance = Impl_IClassFactory_CreateInstance
};
#pragma endregion

static Impl_IGeneric *pClassFactory = NULL;

static HSTRING_HEADER hshAppId;
static HSTRING hsAppId = NULL;

static HSTRING_HEADER hshToastNotificationManager;
static HSTRING hsToastNotificationManager = NULL;

static __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationManagerStatics *pToastNotificationManager = NULL;

static __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *pToastNotifier = NULL;

static HSTRING_HEADER hshToastNotification;
static HSTRING hsToastNotification = NULL;

static __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationFactory *pNotificationFactory = NULL;

static DWORD dwCookie = 0;

static GUID guid;
static WCHAR *guid_str = NULL;
static WCHAR *guid_reg_key = NULL;
static WCHAR *app_reg_key = NULL;

static WCHAR *GetExePath()
{
    DWORD buflen = MAX_PATH;
    WCHAR *path = NULL;
    DWORD len = 0;

    for (;;) {
        WCHAR *ptr = SDL_realloc(path, buflen * sizeof(WCHAR));
        if (!ptr) {
            SDL_free(path);
            return NULL;
        }

        path = ptr;

        len = GetModuleFileNameW(NULL, path, buflen);
        // If this was truncated, then len >= buflen - 1
        if (len < buflen - 1) {
            break;
        }

        // buffer too small? Try again.
        buflen *= 2;
    }

    if (len == 0) {
        SDL_free(path);
        return NULL;
    }

    return path;
}

static WCHAR *GetAppMetadata()
{
    const WCHAR *def = L"Test App Notification";

    return SDL_wcsdup(def);

    const char *app_metadata = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
    if (app_metadata && *app_metadata != '\0') {
        WCHAR *app_id = NULL;
        int id_len = MultiByteToWideChar(CP_UTF8, 0, app_metadata, -1, NULL, 0);
        if (id_len > 0) {
            app_id = SDL_malloc(id_len * sizeof(WCHAR));
            if (!app_id) {
                return NULL;
            }

            MultiByteToWideChar(CP_UTF8, 0, app_metadata, -1, app_id, id_len);
            return app_id;
        }
    }

    return NULL;
}

typedef struct ToastIcon
{
    struct ToastIcon *prev;
    struct ToastIcon *next;

    WCHAR icon_file[1];
} ToastIcon;

static ToastIcon *toast_icons = NULL;
static UINT_PTR cleanup_timer = 0;

static void CleanupIcons()
{
    KillTimer(NULL, cleanup_timer);
    cleanup_timer = 0;

    for (ToastIcon *i = toast_icons; i;) {
        DeleteFileW(i->icon_file);

        ToastIcon *temp = i->next;
        if (i->prev) {
            i->prev->next = i->next;
        }
        if (i->next) {
            i->next->prev = i->prev;
        }
        if (i == toast_icons) {
            toast_icons = temp;
        }

        SDL_free(i);
        i = temp;
    }
}

static void IconCleanupCallback(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    CleanupIcons();
}

static WCHAR *SaveToastIcon(SDL_Surface *icon)
{
    if (icon) {
        // The documentation states that the buffers for these should be MAX_PATH.
        WCHAR *temp_path = NULL;
        DWORD path_len = 0;

        path_len = GetTempPath2W(0, NULL);
        if (!path_len) {
            return NULL;
        }
        temp_path = SDL_realloc(temp_path, (path_len + 1) * sizeof(WCHAR));
        path_len = GetTempPath2W(path_len + 1, temp_path);
        if (!path_len) {
            SDL_free(temp_path);
            return NULL;
        }

        WCHAR file_name[MAX_PATH];
        const UINT name_ret = GetTempFileNameW(temp_path, L"SDL", 0, file_name);
        if (!name_ret) {
            SDL_free(temp_path);
            return NULL;
        }

        path_len += SDL_wcslen(file_name) + 5;

        ToastIcon *toast_icon = SDL_calloc(1, sizeof(ToastIcon) + (path_len * sizeof(WCHAR)));
        toast_icon->next = toast_icons;
        if (toast_icons) {
            toast_icons->prev = toast_icon;
        }
        toast_icons = toast_icon;

        SDL_wcslcat(toast_icon->icon_file, L".png", path_len);

        int len = WideCharToMultiByte(CP_UTF8, 0, toast_icon->icon_file, -1, NULL, 0, NULL, NULL);
        char *png_path = SDL_malloc(len * sizeof(char));
        WideCharToMultiByte(CP_UTF8, 0, toast_icon->icon_file, -1, png_path, len, NULL, NULL);
        SDL_SavePNG(icon, png_path);
        SDL_free(png_path);

        // Schedule a cleanup of icons 5 seconds from now.
        if (cleanup_timer) {
            KillTimer(NULL, cleanup_timer);
        }
        cleanup_timer = SetTimer(NULL, 0, 5000, IconCleanupCallback);

        return toast_icon->icon_file;
    }

    return NULL;
}

static bool InitToastSystem()
{
    static bool initialized = false;

    if (initialized) {
        return true;
    }

    // Initialize COM and Windows runtime with the same threading model.
    RO_INIT_TYPE ro_init_type = RO_INIT_SINGLETHREADED;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        ro_init_type = RO_INIT_MULTITHREADED;
    }
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    hr = RoInitialize(ro_init_type);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    /*
     * Construct the path to our EXE that will be used to launch it when something requests our interface.
     * As said above, registration is dynamic - as long as this app runs, COM knows about the fact that this
     * app implements our INotificationActivationCallback interface. The info here is used when this app has
     * closed and someone clicks the toast notification for example; in that case, since our app is not
     * running, thus CoRegisterClassObject was not called, COM needs info on what EXE contains the implementation
     * of the interface, and we specify that here; without setting this, clicking on notifications will do nothing
     */
    WCHAR *wszExePath = GetExePath();

    int id_len = 0;
    WCHAR *app_id = NULL;
    const char *app_id_utf8 = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
    if (app_id_utf8 && *app_id_utf8 != L'\0') {
        id_len = MultiByteToWideChar(CP_UTF8, 0, app_id_utf8, -1, NULL, 0);
        if (id_len <= 0) {
            return 0;
        }
        app_id = SDL_malloc(id_len * sizeof(WCHAR));
        if (!app_id) {
            return 0;
        }
        MultiByteToWideChar(CP_UTF8, 0, app_id_utf8, -1, app_id, id_len);
    } else {
        app_id = wcsrchr(wszExePath, L'\\');
        if (app_id) {
            ++app_id;
        } else {
            app_id = wszExePath;
        }
        id_len = (int)SDL_wcslen(app_id) + 1;
    }

    const size_t reg_key_len = SDL_wcslen(REG_KEY_BASE) + id_len;
    app_reg_key = SDL_malloc(reg_key_len * sizeof(WCHAR));
    SDL_swprintf(app_reg_key, reg_key_len, L"%S%S", REG_KEY_BASE, app_id);

    /*
     * Allocate class factory. This factory produces our implementation of the INotificationActivationCallback interface.
     * This interface has an ::Activate member method that gets called when someone interacts with the toast notification.
     */
    if (SUCCEEDED(hr)) {
        pClassFactory = SDL_malloc(sizeof(Impl_IGeneric));
        if (!pClassFactory) {
            return false;
        } else {
            pClassFactory->lpVtbl = (IUnknownVtbl *)(&Impl_IClassFactory_Vtbl);
            pClassFactory->dwRefCount = 1;
        }
    }

    ZeroMemory(&guid, sizeof(GUID));
    CoCreateGuid(&guid);

    guid_str = SDL_malloc(128 * sizeof(WCHAR));
    StringFromGUID2(&guid, guid_str, 128);

    guid_reg_key = SDL_malloc(256 * sizeof(WCHAR));
    SDL_swprintf(guid_reg_key, 512, L"SOFTWARE\\Classes\\CLSID\\%S\\LocalServer32", guid_str);

    /*
     * Instead of having to register our COM class in the registry beforehand, we opt to registering it at runtime;
     * we associate our GUID with the class factory that provides our INotificationActivationCallback interface.
     */
    hr = CoRegisterClassObject(&guid, (LPUNKNOWN)pClassFactory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &dwCookie);
    if (FAILED(hr)) {
        return false;
    }

    hr = HRESULT_FROM_WIN32(RegSetValueW(HKEY_CURRENT_USER, guid_reg_key, REG_SZ, wszExePath, (DWORD)SDL_wcslen(wszExePath)));
    if (FAILED(hr)) {
        return 0;
    }

    /*
     * Here we set some info about our app and associate our AUMID with the GUID from above
     * (the one that is associated with our class factory which produces our INotificationActivationCallback interface)
     */
    hr = HRESULT_FROM_WIN32(RegSetKeyValueW(HKEY_CURRENT_USER, app_reg_key, L"DisplayName", REG_SZ, L"Toast Activator Pure C Example", 31 * sizeof(wchar_t)));
    if (FAILED(hr)) {
        return false;
    }

    hr = HRESULT_FROM_WIN32(RegSetKeyValueW(HKEY_CURRENT_USER, app_reg_key, L"CustomActivator", REG_SZ, guid_str, SDL_wcslen(guid_str) * sizeof(wchar_t)));
    if (FAILED(hr)) {
        return false;
    }

    hr = WindowsCreateStringReference(app_id, (UINT32)SDL_wcslen(app_id), &hshAppId, &hsAppId);
    if (FAILED(hr)) {
        return false;
    }

    hr = WindowsCreateStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager, (UINT32)(sizeof(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager) / sizeof(wchar_t) - 1), &hshToastNotificationManager, &hsToastNotificationManager);
    if (FAILED(hr)) {
        return false;
    }

    hr = RoGetActivationFactory(hsToastNotificationManager, &IID_IToastNotificationManagerStatics, (LPVOID *)&pToastNotificationManager);
    if (FAILED(hr)) {
        return false;
    }

    hr = pToastNotificationManager->lpVtbl->CreateToastNotifierWithId(pToastNotificationManager, hsAppId, &pToastNotifier);
    if (FAILED(hr)) {
        return false;
    }

    hr = WindowsCreateStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification, (UINT32)(sizeof(RuntimeClass_Windows_UI_Notifications_ToastNotification) / sizeof(wchar_t) - 1), &hshToastNotification, &hsToastNotification);
    if (FAILED(hr)) {
        return false;
    }

    hr = RoGetActivationFactory(hsToastNotification, &IID_IToastNotificationFactory, (LPVOID *)&pNotificationFactory);
    if (FAILED(hr)) {
        return false;
    }

    initialized = true;
    return true;
}

SDL_NotificationID SDL_SYS_ShowNotification(const SDL_NotificationData *notification_info)
{
    HRESULT hr = S_OK;
    dwMainThreadId = GetCurrentThreadId();

    if (!InitToastSystem()) {
        return 0;
    }

    const WCHAR *icon_path = SaveToastIcon(notification_info->icon);
    if (icon_path) {
        hr = HRESULT_FROM_WIN32(RegSetKeyValueW(HKEY_CURRENT_USER, app_reg_key, L"IconUri", REG_SZ, icon_path, SDL_wcslen(icon_path) * sizeof(wchar_t)));
        if (FAILED(hr)) {
            return false;
        }
    }

    HSTRING_HEADER hshXmlDocument;
    HSTRING hsXmlDocument = NULL;
    if (SUCCEEDED(hr)) {
        hr = WindowsCreateStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument, (UINT32)(sizeof(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument) / sizeof(wchar_t) - 1), &hshXmlDocument, &hsXmlDocument);
    }

    HSTRING_HEADER hshBanner;
    HSTRING hsBanner = NULL;
    if (SUCCEEDED(hr)) {
        hr = WindowsCreateStringReference(wszBannerText, (UINT32)(sizeof(wszBannerText) / sizeof(wchar_t) - 1), &hshBanner, &hsBanner);
    }

    IInspectable *pInspectable = NULL;
    if (SUCCEEDED(hr)) {
        hr = RoActivateInstance(hsXmlDocument, &pInspectable);
    }

    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *pXmlDocument = NULL;
    if (SUCCEEDED(hr)) {
        hr = pInspectable->lpVtbl->QueryInterface(pInspectable, &IID_IXmlDocument, (void **)(&pXmlDocument));
    }

    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocumentIO *pXmlDocumentIO = NULL;
    if (SUCCEEDED(hr)) {
        hr = pXmlDocument->lpVtbl->QueryInterface(pXmlDocument, &IID_IXmlDocumentIO, (void **)(&pXmlDocumentIO));
    }

    if (SUCCEEDED(hr)) {
        hr = pXmlDocumentIO->lpVtbl->LoadXml(pXmlDocumentIO, hsBanner);
    }

    __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *pToastNotification = NULL;
    if (SUCCEEDED(hr)) {
        hr = pNotificationFactory->lpVtbl->CreateToastNotification(pNotificationFactory, pXmlDocument, &pToastNotification);
    }

    if (SUCCEEDED(hr)) {
        hr = pToastNotifier->lpVtbl->Show(pToastNotifier, pToastNotification);
    }

    SDL_PumpEvents();

    if (pToastNotification) {
        pToastNotification->lpVtbl->Release(pToastNotification);
    }
    if (pXmlDocumentIO) {
        pXmlDocumentIO->lpVtbl->Release(pXmlDocumentIO);
    }
    if (pXmlDocument) {
        pXmlDocument->lpVtbl->Release(pXmlDocument);
    }
    if (pInspectable) {
        pInspectable->lpVtbl->Release(pInspectable);
    }
    if (hsBanner) {
        WindowsDeleteString(hsBanner);
    }
    if (hsXmlDocument) {
        WindowsDeleteString(hsXmlDocument);
    }

    return 1;
}

void SDL_CleanupNotifications()
{
    if (pNotificationFactory) {
        pNotificationFactory->lpVtbl->Release(pNotificationFactory);
        pNotificationFactory = NULL;
    }
    if (hsToastNotification) {
        WindowsDeleteString(hsToastNotification);
        hsToastNotification = NULL;
    }
    if (pToastNotifier) {
        pToastNotifier->lpVtbl->Release(pToastNotifier);
        pToastNotifier = NULL;
    }
    if (pToastNotificationManager) {
        pToastNotificationManager->lpVtbl->Release(pToastNotificationManager);
        pToastNotificationManager = NULL;
    }
    if (hsToastNotificationManager) {
        WindowsDeleteString(hsToastNotificationManager);
        hsToastNotificationManager = NULL;
    }
    if (hsAppId) {
        WindowsDeleteString(hsAppId);
        hsAppId = NULL;
    }
    if (dwCookie) {
        CoRevokeClassObject(dwCookie);
        dwCookie = 0;
    }
    if (pClassFactory) {
        pClassFactory->lpVtbl->Release((IUnknown *)pClassFactory);
        pClassFactory = NULL;
    }

    CleanupIcons();

    // Clean up the registry entries.
    RegDeleteKeyW(HKEY_CURRENT_USER, guid_reg_key);
    const int len = SDL_wcslen(guid_reg_key);
    for (int i = len; i > 0; --i) {
        if (guid_reg_key[i] == L'\\') {
            guid_reg_key[i] = L'\0';
            break;
        }
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, guid_reg_key);
    RegDeleteKeyW(HKEY_CURRENT_USER, app_reg_key);

    RoUninitialize();
    CoUninitialize();
}