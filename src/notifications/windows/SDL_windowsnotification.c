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
#include "notifications/SDL_notification_c.h"
#include "video/SDL_surface_c.h"

#include <Windows.ui.notifications.h>
#include <bcrypt.h>
#include <initguid.h>
#include <roapi.h>
#include <winstring.h>

#include "../../core/windows/SDL_windows.h"

/* This is normally found in NotificationActivationCallback.h, however, MinGW
 * doesn't provide this header, so the relevant sections are included manually.
 *
 * TODO: Replace with the actual header if MinGW starts including it.
 */
typedef interface INotificationActivationCallback INotificationActivationCallback;

typedef struct NOTIFICATION_USER_INPUT_DATA
{
    LPCWSTR Key;
    LPCWSTR Value;
} NOTIFICATION_USER_INPUT_DATA;

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
// End NotificationActivationCallback.h content

// The registry key base needed to register the app instance so notifications can be sent.
#define REG_KEY_BASE    L"SOFTWARE\\Classes\\AppUserModelId\\"
#define REG_CLSID_FMT   L"SOFTWARE\\Classes\\CLSID\\%ls\\LocalServer32"
#define GROUP_ID_STRING L"SDL_Notification"

// IIDs for the interfaces.
DEFINE_GUID(IID_IToastNotificationManagerStatics,
            0x50ac103f, 0xd235, 0x4598, 0xbb, 0xef, 0x98, 0xfe, 0x4d, 0x1a, 0x3a, 0xd4);

DEFINE_GUID(IID_IToastNotificationManagerStatics2,
            0x7ab93c52, 0x0e48, 0x4750, 0xba, 0x9d, 0x1a, 0x41, 0x13, 0x98, 0x18, 0x47);

DEFINE_GUID(IID_IToastNotificationFactory,
            0x04124b20, 0x82c6, 0x4229, 0xb1, 0x09, 0xfd, 0x9e, 0xd4, 0x66, 0x2b, 0x53);

DEFINE_GUID(IID_IToastNotification2,
            0x9dfb9fd1, 0x143a, 0x490e, 0x90, 0xbf, 0xb9, 0xfb, 0xa7, 0x13, 0x2d, 0xe7);

DEFINE_GUID(IID_INotificationActivationCallback,
            0x53e31837, 0x6600, 0x4a81, 0x93, 0x95, 0x75, 0xcf, 0xfe, 0x74, 0x6f, 0x94);

DEFINE_GUID(IID_IXmlDocument,
            0xf7f3a506, 0x1e87, 0x42d6, 0xbc, 0xfb, 0xb8, 0xc8, 0x09, 0xfa, 0x54, 0x94);

DEFINE_GUID(IID_IXmlDocumentIO,
            0x6cd0e74e, 0xee65, 0x4489, 0x9e, 0xbf, 0xca, 0x43, 0xe8, 0x7b, 0xa6, 0x37);

DEFINE_GUID(IID_IToastDismissedEventHandler,
            0x61c2402f, 0x0ed0, 0x5a18, 0xab, 0x69, 0x59, 0xf4, 0xaa, 0x99, 0xa3, 0x68);

static struct Impl_IGeneric *pClassFactory = NULL;

static HSTRING hsGroupId = NULL;
static HSTRING hsAppId = NULL;

static __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationManagerStatics *pToastNotificationManager = NULL;
static __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *pToastNotifier = NULL;
static __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationFactory *pNotificationFactory = NULL;

static DWORD dwCookie = 0;

static GUID guid;
static WCHAR *guid_str = NULL;
static WCHAR *guid_reg_key = NULL;
static WCHAR *app_reg_key = NULL;

static bool ro_initialized = false;
static bool co_initialized = false;

// IUnknown implementation
typedef struct Impl_IGeneric
{
    IUnknownVtbl *lpVtbl;
    SDL_AtomicInt refCount;
} Impl_IGeneric;

static ULONG STDMETHODCALLTYPE Impl_IGeneric_AddRef(Impl_IGeneric *_this)
{
    return SDL_AddAtomicInt(&_this->refCount, 1) + 1;
}

static ULONG STDMETHODCALLTYPE Impl_IGeneric_Release(Impl_IGeneric *_this)
{
    int newRefCount = SDL_AddAtomicInt(&_this->refCount, -1) - 1;
    if (!newRefCount) {
        SDL_free(_this);
    }
    return (ULONG)newRefCount;
}

// INotificationActivationCallback implementation
static HRESULT STDMETHODCALLTYPE Impl_INotificationActivationCallback_QueryInterface(INotificationActivationCallback *_this, REFIID riid, void **ppvObject)
{
    if (ppvObject == NULL) {
        return E_POINTER;
    }
    if (IsEqualGUID(riid, &IID_INotificationActivationCallback) ||
        IsEqualGUID(riid, &IID_IAgileObject) ||
        IsEqualGUID(riid, &IID_IUnknown)) {
        *ppvObject = _this;
        _this->lpVtbl->AddRef(_this);
        return S_OK;
    }

    return E_NOINTERFACE;
}

// This is what is called when the user interacts with a notification.
static HRESULT STDMETHODCALLTYPE Impl_INotificationActivationCallback_Activate(INotificationActivationCallback *_this, LPCWSTR appUserModelId, LPCWSTR invokedArgs, const NOTIFICATION_USER_INPUT_DATA *data, ULONG count)
{
    SDL_NotificationID id = 0;
    if (invokedArgs && *invokedArgs != L'\0') {
        const int len = WideCharToMultiByte(CP_UTF8, 0, invokedArgs, -1, NULL, 0, NULL, NULL);
        if (len <= 0) {
            goto done;
        }
        char *utf8_args = SDL_malloc(len * sizeof(char));
        if (!utf8_args) {
            goto done;
        }
        WideCharToMultiByte(CP_UTF8, 0, invokedArgs, -1, utf8_args, len, NULL, NULL);

        char action[256];
        const int ret = SDL_sscanf(utf8_args, "notification_id=%" SDL_PRIu32 ",action=%s", &id, action);
        SDL_free(utf8_args);
        if (ret != 2) {
            goto done;
        }

        SDL_SendNotificationAction(id, action);
    }

done:
    return S_OK;
}

static const INotificationActivationCallbackVtbl Impl_INotificationActivationCallback_Vtbl = {
    .QueryInterface = (void *)Impl_INotificationActivationCallback_QueryInterface,
    .AddRef = (void *)Impl_IGeneric_AddRef,
    .Release = (void *)Impl_IGeneric_Release,
    .Activate = Impl_INotificationActivationCallback_Activate
};

// IClassFactory
static HRESULT STDMETHODCALLTYPE Impl_IClassFactory_QueryInterface(Impl_IGeneric *_this, REFIID riid, void **ppvObject)
{
    if (ppvObject == NULL) {
        return E_POINTER;
    }
    if (IsEqualGUID(riid, &IID_IClassFactory) ||
        IsEqualGUID(riid, &IID_IAgileObject) ||
        IsEqualGUID(riid, &IID_IUnknown)) {
        *ppvObject = _this;
        _this->lpVtbl->AddRef((IUnknown *)_this);
        return S_OK;
    }

    return E_NOINTERFACE;
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
            SDL_SetAtomicInt(&thisobj->refCount, 1);
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

// OnDismissed interface
static HRESULT STDMETHODCALLTYPE Impl_OnDismissed_QueryInterface(__FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs *_this, REFIID riid, void **ppvObject)
{
    if (ppvObject == NULL) {
        return E_POINTER;
    }
    if (IsEqualGUID(riid, &IID_IToastDismissedEventHandler) ||
        IsEqualGUID(riid, &IID_IAgileObject) ||
        IsEqualGUID(riid, &IID_IUnknown)) {
        *ppvObject = _this;
        _this->lpVtbl->AddRef(_this);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE Impl_OnDismissed_Invoke(__FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs *_this, __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *Sender, __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *Args)
{
    __x_ABI_CWindows_CUI_CNotifications_CToastDismissalReason Reason;
    Args->lpVtbl->get_Reason(Args, &Reason);

    /* Remove transient notifications that were cancelled or timed out,
     * so they won't persist in the notification center.
     */
    switch (Reason) {
    case ToastDismissalReason_TimedOut:
    case ToastDismissalReason_UserCanceled:
        pToastNotifier->lpVtbl->Hide(pToastNotifier, Sender);
        break;

    default:
        break;
    }

    return S_OK;
}

static __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgsVtbl WindowsToast__OnDismissedVtbl = {
    .QueryInterface = &Impl_OnDismissed_QueryInterface,
    .AddRef = (void *)Impl_IGeneric_AddRef,
    .Release = (void *)Impl_IGeneric_AddRef,
    .Invoke = &Impl_OnDismissed_Invoke,
};

static __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs OnDismissed = {
    .lpVtbl = &WindowsToast__OnDismissedVtbl
};

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

/* There is no way to pass an image as a byte stream when creating Windows
 * notifications, so surfaces are saved as PNG files to temporary storage
 * while the notification is being shown, then cleaned up a few seconds later.
 */
typedef struct ToastIcon
{
    struct ToastIcon *next;
    WCHAR icon_file[1];
} ToastIcon;

static ToastIcon *toast_icons = NULL;
static UINT_PTR cleanup_timer_id = 0;

static void CleanupIcons()
{
    KillTimer(NULL, cleanup_timer_id);
    cleanup_timer_id = 0;

    for (ToastIcon *i = toast_icons; i; i = toast_icons) {
        DeleteFileW(i->icon_file);
        toast_icons = i->next;
        SDL_free(i);
    }
}

static void CALLBACK IconCleanupCallback(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    CleanupIcons();
}

static WCHAR *SaveToastIcon(SDL_Surface *icon, bool is_header)
{
    if (icon) {
        // The documentation states that the buffers for these should be MAX_PATH.
        WCHAR *temp_path = NULL;
        size_t path_len = 0;

        // Stop any timers so they won't fire in the middle of this.
        if (!is_header && cleanup_timer_id) {
            KillTimer(NULL, cleanup_timer_id);
        }

        path_len = GetTempPath2W(0, NULL);
        if (!path_len) {
            return NULL;
        }
        temp_path = SDL_realloc(temp_path, (path_len + 1) * sizeof(WCHAR));
        path_len = GetTempPath2W((DWORD)path_len + 1, temp_path);
        if (!path_len) {
            SDL_free(temp_path);
            return NULL;
        }

        WCHAR file_name[MAX_PATH];
        if (is_header) {
            const Uint32 hash = SDL_murmur3_32(icon->pixels, (size_t)(icon->pitch * icon->h), 0);
            SDL_swprintf(file_name, MAX_PATH, L"%ls_%" SDL_PRIu32, guid_str, hash);
        } else {
            const UINT name_ret = GetTempFileNameW(temp_path, L"SDL", 0, file_name);
            if (!name_ret) {
                SDL_free(temp_path);
                return NULL;
            }
        }

        path_len += SDL_wcslen(file_name) + 5;

        WCHAR *path_buf = NULL;
        if (!is_header) {
            ToastIcon *toast_icon = SDL_calloc(1, sizeof(ToastIcon) + (path_len * sizeof(WCHAR)));
            toast_icon->next = toast_icons;
            toast_icons = toast_icon;
            path_buf = toast_icon->icon_file;
        } else {
            path_buf = SDL_calloc(path_len, sizeof(WCHAR));
            SDL_wcslcpy(path_buf, temp_path, path_len);
        }

        SDL_wcslcat(path_buf, file_name, path_len);
        SDL_wcslcat(path_buf, L".png", path_len);

        const int len = WideCharToMultiByte(CP_UTF8, 0, path_buf, -1, NULL, 0, NULL, NULL);
        char *png_path = SDL_malloc(len * sizeof(char));
        WideCharToMultiByte(CP_UTF8, 0, path_buf, -1, png_path, len, NULL, NULL);
        SDL_SavePNG(icon, png_path);
        SDL_free(png_path);

        if (!is_header) {
            // Duplicate the path, since the source object will be destroyed by a timer.
            path_buf = SDL_wcsdup(path_buf);

            // Schedule a cleanup of icons 5 seconds from now.
            cleanup_timer_id = SetTimer(NULL, 0, 5000, IconCleanupCallback);
        }

        return path_buf;
    }

    return NULL;
}

static bool InitGUID()
{
    HRESULT hr;
    SDL_zero(guid);

    WCHAR tmp_guid_str[128];
    DWORD tmp_size = sizeof(tmp_guid_str);
    const LONG ret = RegGetValueW(HKEY_CURRENT_USER, app_reg_key, L"CustomActivator", RRF_RT_REG_SZ, NULL, &tmp_guid_str, &tmp_size);
    if (ret == 0) { // ERROR_SUCCESS
        hr = CLSIDFromString(tmp_guid_str, &guid);
        if (SUCCEEDED(hr)) {
            guid_str = SDL_wcsdup(tmp_guid_str);
            goto format_reg_key;
        }
    }

    hr = CoCreateGuid(&guid);
    if (FAILED(hr)) {
        return false;
    }

    guid_str = SDL_malloc(128 * sizeof(WCHAR));
    if (!guid_str) {
        return false;
    }
    StringFromGUID2(&guid, guid_str, 128);

format_reg_key:
    const size_t key_size = sizeof(REG_CLSID_FMT) + 128;
    guid_reg_key = SDL_malloc(key_size);
    SDL_swprintf(guid_reg_key, key_size, REG_CLSID_FMT, guid_str);

    return true;
}

static WCHAR *GetAppMetadata(const char *metadata_name)
{
    WCHAR *metadata = NULL;
    int id_len = 0;

    const char *app_metadata = SDL_GetAppMetadataProperty(metadata_name);
    if (app_metadata && *app_metadata != '\0') {
        id_len = MultiByteToWideChar(CP_UTF8, 0, app_metadata, -1, NULL, 0);
        if (id_len > 0) {
            metadata = SDL_malloc(id_len * sizeof(WCHAR));
            if (!metadata) {
                return NULL;
            }

            MultiByteToWideChar(CP_UTF8, 0, app_metadata, -1, metadata, id_len);
            return metadata;
        }
    } else {
        WCHAR *wszExePath = GetExePath();

        metadata = wcsrchr(wszExePath, L'\\');
        if (metadata) {
            ++metadata;
        } else {
            metadata = wszExePath;
        }

        metadata = SDL_wcsdup(metadata);
        SDL_free(wszExePath);

        return metadata;
    }

    return NULL;
}

static bool InitToastSystem()
{
    static bool initialized = false;

    if (initialized) {
        return true;
    }

    HSTRING_HEADER hshToastNotificationManager;
    HSTRING hsToastNotificationManager = NULL;
    HSTRING_HEADER hshToastNotification;
    HSTRING hsToastNotification = NULL;
    WCHAR *image_path = NULL;

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
    co_initialized = true;

    hr = RoInitialize(ro_init_type);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        return false;
    }
    ro_initialized = true;

    // Get the execuable path
    WCHAR *wszExePath = GetExePath();

    // Get the application ID and name.
    WCHAR *app_id = GetAppMetadata(SDL_PROP_APP_METADATA_IDENTIFIER_STRING);
    WCHAR *app_name = GetAppMetadata(SDL_PROP_APP_METADATA_NAME_STRING);

    // Create the group string;
    hr = WindowsCreateString(GROUP_ID_STRING, (UINT32)SDL_wcslen(GROUP_ID_STRING), &hsGroupId);
    if (FAILED(hr)) {
        goto cleanup;
    }

    // Create the persistent appID string.
    hr = WindowsCreateString(app_id, (UINT32)SDL_wcslen(app_id), &hsAppId);
    if (FAILED(hr)) {
        goto cleanup;
    }

    // Build the registry key.
    {
        size_t reg_key_len = SDL_wcslen(REG_KEY_BASE) + SDL_wcslen(app_id) + 1;
        app_reg_key = SDL_malloc(reg_key_len * sizeof(WCHAR));
        if (!app_reg_key) {
            goto cleanup;
        }
        SDL_swprintf(app_reg_key, reg_key_len, L"%ls%ls", REG_KEY_BASE, app_id);
    }

    /* Allocate class factory. This factory produces our implementation of the INotificationActivationCallback interface.
     * This interface has an ::Activate member method that gets called when someone interacts with the toast notification.
     */
    pClassFactory = SDL_malloc(sizeof(Impl_IGeneric));
    if (!pClassFactory) {
        goto cleanup;
    }
    pClassFactory->lpVtbl = (IUnknownVtbl *)(&Impl_IClassFactory_Vtbl);
    SDL_SetAtomicInt(&pClassFactory->refCount, 1);

    /* Initialize the GUID. If a registry key for this app already exists,
     * the existing key will be reused. Otherwise, a new key will be created.
     */
    if (!InitGUID()) {
        goto cleanup;
    }

    /*
     * Instead of having to register our COM class in the registry beforehand, we opt to registering it at runtime;
     * we associate our GUID with the class factory that provides our INotificationActivationCallback interface.
     */
    hr = CoRegisterClassObject(&guid, (LPUNKNOWN)pClassFactory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &dwCookie);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = HRESULT_FROM_WIN32(RegSetValueW(HKEY_CURRENT_USER, guid_reg_key, REG_SZ, wszExePath, (DWORD)SDL_wcslen(wszExePath)));
    if (FAILED(hr)) {
        goto cleanup;
    }

    {
        /* Set the app name, icon, and associate our AUMID with the GUID from above.
         * Windows notifications load images from disk, so save the header icon to temporary storage.
         */
        SDL_Surface *image = SDL_GetPointerProperty(SDL_GetGlobalProperties(), SDL_PROP_GLOBAL_NOTIFICATION_HEADER_ICON_POINTER, NULL);

        image_path = SaveToastIcon(image, true);
        if (image_path) {
            hr = HRESULT_FROM_WIN32(RegSetKeyValueW(HKEY_CURRENT_USER, app_reg_key, L"IconUri", REG_SZ, image_path, (DWORD)(SDL_wcslen(image_path) * sizeof(WCHAR))));
            if (FAILED(hr)) {
                goto cleanup;
            }
        } else {
            // This will "fail" if the key already doesn't exist, which is fine.
            RegDeleteKeyValueW(HKEY_CURRENT_USER, app_reg_key, L"IconUri");
        }
    }

    hr = HRESULT_FROM_WIN32(RegSetKeyValueW(HKEY_CURRENT_USER, app_reg_key, L"DisplayName", REG_SZ, app_name, (DWORD)(SDL_wcslen(app_name) * sizeof(WCHAR))));
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = HRESULT_FROM_WIN32(RegSetKeyValueW(HKEY_CURRENT_USER, app_reg_key, L"CustomActivator", REG_SZ, guid_str, (DWORD)(SDL_wcslen(guid_str) * sizeof(WCHAR))));
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = WindowsCreateStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager, sizeof(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager) / sizeof(WCHAR) - 1, &hshToastNotificationManager, &hsToastNotificationManager);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = RoGetActivationFactory(hsToastNotificationManager, &IID_IToastNotificationManagerStatics, (LPVOID *)&pToastNotificationManager);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = pToastNotificationManager->lpVtbl->CreateToastNotifierWithId(pToastNotificationManager, hsAppId, &pToastNotifier);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = WindowsCreateStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification, (UINT32)(sizeof(RuntimeClass_Windows_UI_Notifications_ToastNotification) / sizeof(wchar_t) - 1), &hshToastNotification, &hsToastNotification);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = RoGetActivationFactory(hsToastNotification, &IID_IToastNotificationFactory, (LPVOID *)&pNotificationFactory);
    if (FAILED(hr)) {
        goto cleanup;
    }

    initialized = true;

cleanup:
    WindowsDeleteString(hsToastNotificationManager);
    WindowsDeleteString(hsToastNotification);
    SDL_free(image_path);

    return initialized;
}

static bool AppendXmlAudio(SDL_IOStream *dst, bool silent)
{
    WCHAR static_buf[256];
    WCHAR *buf = static_buf;
    int buf_len = SDL_arraysize(static_buf);

    const int len = SDL_swprintf(buf, buf_len, L"<audio src=\"ms-winsoundevent:Notification.Default\" loop=\"false\" silent=\"%ls\"/>", silent ? L"true" : L"false");
    const size_t size = len * sizeof(WCHAR);
    return SDL_WriteIO(dst, buf, size) == size;
}

static bool AppendXmlAction(SDL_IOStream *dst, SDL_NotificationID id, const char *label, const char *action)
{
    char static_buf[512];
    char *buf = static_buf;
    int buf_len = SDL_arraysize(static_buf);

    for (;;) {
        int ret = SDL_snprintf(buf, buf_len, "<action content=\"%s\" activationType=\"foreground\" arguments=\"notification_id=%" SDL_PRIu32 ",action=%s\"/>", label, id, action);
        if (ret < buf_len) {
            buf_len = ret + 1;
            break;
        }

        buf_len = ret + 1;
        buf = SDL_realloc(buf, ret);
        if (!buf) {
            return false;
        }
    }

    // We know that, at most, an equal number of wide chars are needed for conversion.
    WCHAR *wcbuf = SDL_malloc(buf_len * sizeof(WCHAR));
    int wclen = 0;
    bool ret = true;

    if (!wcbuf) {
        ret = false;
        goto done;
    }
    wclen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, wcbuf, buf_len);
    if (wclen <= 0) {
        ret = false;
        goto done;
    }

    const size_t size = (wclen - 1) * sizeof(WCHAR);
    if (SDL_WriteIO(dst, wcbuf, size) < size) {
        ret = false;
    }

done:
    SDL_free(wcbuf);
    if (buf != static_buf) {
        SDL_free(buf);
    }

    return ret;
}

static bool AppendXmlImage(SDL_IOStream *dst, const WCHAR *image_path)
{
    WCHAR static_buf[512];
    WCHAR *buf = static_buf;
    int buf_len = SDL_arraysize(static_buf);
    bool ret = true;

    for (;;) {
        const int len = SDL_swprintf(buf, buf_len, L"<image id=\"1\" placement=\"appLogoOverride\" src=\"file:///%ls\"></image>", image_path);
        if (len < buf_len) {
            const size_t size = len * sizeof(WCHAR);
            if (SDL_WriteIO(dst, buf, size) < size) {
                ret = false;
            }
            break;
        }

        buf_len = len + 1;
        buf = SDL_realloc(buf, len * sizeof(WCHAR));
        if (!buf) {
            return false;
        }
    }

    if (buf != static_buf) {
        SDL_free(buf);
    }

    return ret;
}

#define XML_TOAST_OPENING_STR                                                                   \
    L"<toast scenario=\"default\" activationType=\"foreground\" launch=\"action=mainContent\">" \
    L"<visual>"                                                                                 \
    L"<binding template=\"ToastGeneric\">"
#define XML_TOAST_CLOSING_STR   L"</toast>"
#define XML_TEXT_OPENING_STR    L"<text><![CDATA["
#define XML_TEXT_CLOSING_STR    L"]]></text>"
#define XML_ACTIONS_OPENING_STR L"<actions>"
#define XML_ACTIONS_CLOSING_STR L"</actions>"
#define XML_VISUAL_CLOSING_STR  L"</binding></visual>"

static bool AppendXmlText(SDL_IOStream *dst, const char *text)
{
    const int wclen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wclen <= 0) {
        return false;
    }
    WCHAR *wcbuf = SDL_malloc(wclen * sizeof(WCHAR));
    if (!wcbuf) {
        return false;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wcbuf, wclen);

    size_t size = sizeof(XML_TEXT_OPENING_STR) - sizeof(WCHAR);
    if (SDL_WriteIO(dst, XML_TEXT_OPENING_STR, size) < size) {
        return false;
    }

    size = (wclen - 1) * sizeof(WCHAR);
    if (SDL_WriteIO(dst, wcbuf, size) < size) {
        return false;
    }

    size = sizeof(XML_TEXT_CLOSING_STR) - sizeof(WCHAR);
    if (SDL_WriteIO(dst, XML_TEXT_CLOSING_STR, size) < size) {
        return false;
    }

    return true;
}

static WCHAR *BuildNotificationXml(const SDL_NotificationData *notification_info, const WCHAR *icon_path, SDL_NotificationID id, Uint32 *wchar_count)
{
    WCHAR *xml = NULL;
    SDL_IOStream *dst = SDL_IOFromDynamicMem();

    size_t size = sizeof(XML_TOAST_OPENING_STR) - sizeof(WCHAR);
    if (SDL_WriteIO(dst, XML_TOAST_OPENING_STR, size) < size) {
        goto done;
    }
    if (!AppendXmlImage(dst, icon_path)) {
        goto done;
    }
    if (!AppendXmlText(dst, notification_info->title)) {
        goto done;
    }
    if (!AppendXmlText(dst, notification_info->message)) {
        goto done;
    }

    size = sizeof(XML_VISUAL_CLOSING_STR) - sizeof(WCHAR);
    if (SDL_WriteIO(dst, XML_VISUAL_CLOSING_STR, size) < size) {
        goto done;
    }

    if (!AppendXmlAudio(dst, (notification_info->flags & SDL_NOTIFICATION_SILENT) != 0)) {
        goto done;
    }

    if (notification_info->num_actions) {
        size = sizeof(XML_ACTIONS_OPENING_STR) - sizeof(WCHAR);
        if (SDL_WriteIO(dst, XML_ACTIONS_OPENING_STR, size) < size) {
            goto done;
        }

        for (int i = 0; i < notification_info->num_actions; ++i) {
            if (!AppendXmlAction(dst, id, notification_info->actions[i].button_label, notification_info->actions[i].button_id)) {
                goto done;
            }
        }

        size = sizeof(XML_ACTIONS_CLOSING_STR) - sizeof(WCHAR);
        if (SDL_WriteIO(dst, XML_ACTIONS_CLOSING_STR, size) < size) {
            goto done;
        }
    }

    // The XML string *must* be null-terminated for WindowsCreateStringReference().
    size = sizeof(XML_TOAST_CLOSING_STR);
    if (SDL_WriteIO(dst, XML_TOAST_CLOSING_STR, size) < size) {
        goto done;
    }
    *wchar_count = (Uint32)((SDL_TellIO(dst) / sizeof(WCHAR)) - 1);

    // Get the pointer to the XML string.
    SDL_PropertiesID io_props = SDL_GetIOProperties(dst);
    xml = SDL_GetPointerProperty(io_props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
    SDL_SetPointerProperty(io_props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);

done:
    SDL_CloseIO(dst);
    return xml;
}

static void ClearNotificationWithID(SDL_NotificationID id)
{
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationHistory *pToastNotificationHistory = NULL;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationManagerStatics2 *pToastNotificationManagerStatics2 = NULL;
    HSTRING_HEADER hshTag;
    HSTRING hsTag = NULL;
    WCHAR tag[32];

    HRESULT hr = pToastNotificationManager->lpVtbl->QueryInterface(pToastNotificationManager, &IID_IToastNotificationManagerStatics2, (LPVOID *)&pToastNotificationManagerStatics2);
    if (FAILED(hr)) {
        return;
    }
    hr = pToastNotificationManagerStatics2->lpVtbl->get_History(pToastNotificationManagerStatics2, &pToastNotificationHistory);
    if (FAILED(hr)) {
        goto cleanup;
    }

    SDL_swprintf(tag, SDL_arraysize(tag), L"%" SDL_PRIu32, id);
    hr = WindowsCreateStringReference(tag, SDL_wcslen(tag), &hshTag, &hsTag);
    if (FAILED(hr)) {
        goto cleanup;
    }

    pToastNotificationHistory->lpVtbl->RemoveGroupedTagWithId(pToastNotificationHistory, hsTag, hsGroupId, hsAppId);

cleanup:
    WindowsDeleteString(hsTag);
    if (pToastNotificationHistory) {
        pToastNotificationHistory->lpVtbl->Release(pToastNotificationHistory);
    }
    if (pToastNotificationManagerStatics2) {
        pToastNotificationManagerStatics2->lpVtbl->Release(pToastNotificationManagerStatics2);
    }
}

static void SDL_SYS_CleanupNotifications(bool cleanup_reg_keys);

SDL_NotificationID SDL_SYS_ShowNotification(const SDL_NotificationData *notification_info)
{
    HRESULT hr = S_OK;
    SDL_NotificationID ret = 0;

    // Need Win10 or higher for notifications.
    if (!WIN_IsWindows10OrGreater()) {
        SDL_SetError("Notifications require Windows 10 or higher");
        return 0;
    }

    if (!InitToastSystem()) {
        SDL_SYS_CleanupNotifications(true);
        return 0;
    }

    if (notification_info->replaces) {
        ClearNotificationWithID(notification_info->replaces);
    }

    SDL_NotificationID new_id = 0;
    // Generate a unique notification ID.
    if (BCryptGenRandom(NULL, (PUCHAR)&new_id, sizeof(new_id), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) { // STATUS_SUCCESS == 0
        // No RNG? Use the low 32 bits of the current time.
        new_id = (SDL_NotificationID)SDL_GetTicksNS();
    }

    // Windows notifications load images from disk, so save it to temporary storage.
    WCHAR *image_path = SaveToastIcon(notification_info->icon, false);

    // Build the XML description for the notification.
    Uint32 xml_len = 0;
    WCHAR *xml = BuildNotificationXml(notification_info, image_path, new_id, &xml_len);
    SDL_free(image_path);
    if (!xml) {
        return 0;
    }

    HSTRING_HEADER hshXmlDocument;
    HSTRING hsXmlDocument = NULL;
    HSTRING_HEADER hshBanner;
    HSTRING hsBanner = NULL;
    IInspectable *pInspectable = NULL;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *pXmlDocument = NULL;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocumentIO *pXmlDocumentIO = NULL;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *pToastNotification = NULL;

    hr = WindowsCreateStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument, (UINT32)(sizeof(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument) / sizeof(WCHAR) - 1), &hshXmlDocument, &hsXmlDocument);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = WindowsCreateStringReference(xml, xml_len, &hshBanner, &hsBanner);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = RoActivateInstance(hsXmlDocument, &pInspectable);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = pInspectable->lpVtbl->QueryInterface(pInspectable, &IID_IXmlDocument, (void **)(&pXmlDocument));
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = pXmlDocument->lpVtbl->QueryInterface(pXmlDocument, &IID_IXmlDocumentIO, (void **)(&pXmlDocumentIO));
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = pXmlDocumentIO->lpVtbl->LoadXml(pXmlDocumentIO, hsBanner);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = pNotificationFactory->lpVtbl->CreateToastNotification(pNotificationFactory, pXmlDocument, &pToastNotification);
    if (FAILED(hr)) {
        goto cleanup;
    }

    // Register the OnDismissed notifier to clear transient notifications when cancelled or timed out.
    if (notification_info->flags & SDL_NOTIFICATION_TRANSIENT) {
        EventRegistrationToken dismissedToken;
        hr = pToastNotification->lpVtbl->add_Dismissed(pToastNotification, &OnDismissed, &dismissedToken);
        if (FAILED(hr)) {
            goto cleanup;
        }
    }

    // Tag with the ID for future replacement.
    {
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification2 *pToastNotification2;
        HSTRING_HEADER hshTag;
        HSTRING hsTag = NULL;
        WCHAR tag[32];

        hr = pToastNotification->lpVtbl->QueryInterface(pToastNotification, &IID_IToastNotification2, (LPVOID *)&pToastNotification2);
        if (FAILED(hr)) {
            goto cleanup;
        }

        SDL_swprintf(tag, SDL_arraysize(tag), L"%" SDL_PRIu32, new_id);
        hr = WindowsCreateStringReference(tag, SDL_wcslen(tag), &hshTag, &hsTag);
        if (FAILED(hr)) {
            goto cleanup;
        }

        hr = pToastNotification2->lpVtbl->put_Group(pToastNotification2, hsGroupId);
        if (FAILED(hr)) {
            goto cleanup;
        }

        hr = pToastNotification2->lpVtbl->put_Tag(pToastNotification2, hsTag);
        if (FAILED(hr)) {
            goto cleanup;
        }

        pToastNotification2->lpVtbl->Release(pToastNotification2);
    }

    // Finally, show the notification.
    hr = pToastNotifier->lpVtbl->Show(pToastNotifier, pToastNotification);
    if (SUCCEEDED(hr)) {
        ret = new_id;
    }

cleanup:
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

    SDL_free(xml);

    return ret;
}

static void SDL_SYS_CleanupNotifications(bool cleanup_reg_keys)
{
    if (pNotificationFactory) {
        pNotificationFactory->lpVtbl->Release(pNotificationFactory);
        pNotificationFactory = NULL;
    }
    if (pToastNotifier) {
        pToastNotifier->lpVtbl->Release(pToastNotifier);
        pToastNotifier = NULL;
    }
    if (pToastNotificationManager) {
        pToastNotificationManager->lpVtbl->Release(pToastNotificationManager);
        pToastNotificationManager = NULL;
    }
    if (dwCookie) {
        CoRevokeClassObject(dwCookie);
        dwCookie = 0;
    }
    if (pClassFactory) {
        pClassFactory->lpVtbl->Release((IUnknown *)pClassFactory);
        pClassFactory = NULL;
    }
    if (hsAppId) {
        WindowsDeleteString(hsAppId);
        hsAppId = NULL;
    }
    if (hsGroupId) {
        WindowsDeleteString(hsGroupId);
        hsGroupId = NULL;
    }

    CleanupIcons();

    // Clean up the registry entries.
    if (guid_reg_key) {
        RegDeleteKeyW(HKEY_CURRENT_USER, guid_reg_key);
        const size_t len = SDL_wcslen(guid_reg_key);
        for (size_t i = len; i > 0; --i) {
            if (guid_reg_key[i] == L'\\') {
                guid_reg_key[i] = L'\0';
                break;
            }
        }
        RegDeleteKeyW(HKEY_CURRENT_USER, guid_reg_key);

        if (app_reg_key && cleanup_reg_keys) {
            RegDeleteKeyW(HKEY_CURRENT_USER, app_reg_key);
        }
    }

    SDL_free(guid_reg_key);
    SDL_free(app_reg_key);
    guid_reg_key = NULL;
    app_reg_key = NULL;

    if (ro_initialized) {
        RoUninitialize();
        ro_initialized = false;
    }
    if (co_initialized) {
        CoUninitialize();
        co_initialized = false;
    }
}

void SDL_CleanupNotifications()
{
    SDL_SYS_CleanupNotifications(false);
}