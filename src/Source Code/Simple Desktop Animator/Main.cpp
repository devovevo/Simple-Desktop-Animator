#include <windows.h>
#include <WinUser.h>
#include <chrono>
#include <thread>
#include <mfmediaengine.h>
#include <mfapi.h>
#include <iostream>
#include <d3dcommon.h>
#include <dxgi.h>
#include <Audioclient.h>
#include <d3d11.h>
#include <conio.h>
#include <fstream>
#include <strsafe.h>
#include <dcomp.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

const WCHAR CLASS_NAME[] = L"MFVideoEVR Window Class";
const WCHAR WINDOW_NAME[] = L"MFVideoEVR";
HWND videoWindowHandle = NULL;
HWND DesktopWindow = NULL;

wchar_t file[500];

void drawToWindow();
HWND FindDTWindow();
DWORD InitializeWindow(LPVOID tP);

ID3D11Texture2D* spTextureDst;

D3DFORMAT format = D3DFMT_X8R8G8B8;

//const COLORREF rgbBlack = 0x00cbc0ff;
const COLORREF rgbBlack = 0x00000000;

BOOL play = TRUE;

int extraY = 300;
int x, y;
//MFARGB backgroundColor = { 203, 192, 255, 255 };
MFARGB backgroundColor = { 0, 0, 0, 255 };

IMFMediaEngine* m_spMediaEngine;
IMFMediaEngineEx* m_spEngineEx;

#define VIDEO_WIDTH (GetSystemMetrics(SM_CXFULLSCREEN))
#define VIDEO_HEIGHT (GetSystemMetrics(SM_CYFULLSCREEN) + 100)

IDXGIOutput* spOutput;
IDXGISwapChain* swapChain;

double conversionHW, conversionWH;

using namespace std;

int rows = 3, cols = 3, delay = 100;
double audio = -1;

int multi = 1;

CRITICAL_SECTION critSec;

DWORD w, h;

// MediaEngineNotify: Implements the callback for Media Engine event notification.
class MediaEngineNotify : public IMFMediaEngineNotify
{
    long m_cRef;
    HWND m_cWindow = NULL;

public:
    MediaEngineNotify() : m_cRef(1), m_cWindow(videoWindowHandle)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (__uuidof(IMFMediaEngineNotify) == riid)
        {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
        }
        else
        {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }

        AddRef();

        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0)
        {
            delete this;
        }
        return cRef;
    }

    // EventNotify is called when the Media Engine sends an event.
    STDMETHODIMP EventNotify(DWORD meEvent, DWORD_PTR param1, DWORD param2)
    {
        if (meEvent == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE)
        {
            SetEvent(reinterpret_cast<HANDLE>(param1));
        }
        else
        {
            //m_pCB->OnMediaEngineEvent(meEvent);
        }

        return S_OK;
    }

    void MediaEngineNotifyCallback()
    {
        //m_pCB = pCB;
    }
};

int main()
{
    InitializeCriticalSection(&critSec);


    OPENFILENAME ofn;       // common dialog box structure

        // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrFilter = L"Video Files\0*.MP4;*.MP3;*.MPEG;*.MPG;*.MOV;*.WMV;*.YUV;*.MTS\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn) == TRUE)
    {
        wcout << file << endl;
    }

    ifstream f(file);

    if (f.good())
    {
        cout << "How many columns would you like? ";
        cin >> cols;

        cout << "How many rows would you like? ";
        cin >> rows;

        cout << "Audio (Decimal value, 0 - Silence, 1 - Full Volume)? ";
        cin >> audio;
        
        while (audio > 1 || audio < 0)
        {
            cin >> audio;
        }

        cout << "Would you like Multi (1) or Single (0) Thread (Multi less glitchy > CPU, single more glitchy, < CPU)? ";
        cin >> multi;

        while (multi != 0 && multi != 1)
        {
            cout << "Invalid Input. Multi (1) or Single(1) Thread? ";
            cin >> multi;
        }

        cout << "Delay for frames (nanoseconds, default is 100)? ";
        cin >> delay;

        if (CoInitializeEx(NULL, NULL) == S_OK)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitializeWindow, NULL, 0, NULL);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            //videoWindowHandle = FindDTWindow();

            DesktopWindow = FindDTWindow();

            SetParent(videoWindowHandle, DesktopWindow);

            IMFMediaEngineClassFactory* spFactory;
            IMFAttributes* spAttributes;
            MediaEngineNotify* spNotify;

            MFStartup(MF_VERSION);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            EnterCriticalSection(&critSec);

            DXGI_SWAP_CHAIN_DESC sd;
            ZeroMemory(&sd, sizeof(sd));
            sd.BufferCount = 1;
            sd.BufferDesc.Width = VIDEO_WIDTH;
            sd.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
            sd.BufferDesc.Height = VIDEO_HEIGHT;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferDesc.RefreshRate.Numerator = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = videoWindowHandle;
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
            sd.Windowed = TRUE;

            HRESULT hr = S_OK;
            D3D_FEATURE_LEVEL FeatureLevel;

            ID3D11Device* device;
            ID3D11DeviceContext* context;

            D3D_FEATURE_LEVEL FeatureLevels = D3D_FEATURE_LEVEL_11_0;

            hr = D3D11CreateDeviceAndSwapChain(NULL,
                D3D_DRIVER_TYPE_HARDWARE,
                NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                &FeatureLevels,
                1,
                D3D11_SDK_VERSION,
                &sd,
                &swapChain,
                &device,
                &FeatureLevel,
                &context);

            if (FAILED(hr))
            {
                return hr;
            }

            UINT resetToken;
            IMFDXGIDeviceManager* deviceManager;
            MFCreateDXGIDeviceManager(&resetToken, &deviceManager);

            HANDLE deviceHandle;
            deviceManager->OpenDeviceHandle(&deviceHandle);

            deviceManager->LockDevice(deviceHandle, IID_PPV_ARGS(&device), TRUE);

            deviceManager->ResetDevice(device, resetToken);

            spNotify = new MediaEngineNotify();

            CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spFactory));

            MFCreateAttributes(&spAttributes, 1);

            spAttributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, (IUnknown*)spNotify);
            spAttributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, (IUnknown*)deviceManager);
            spAttributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, format);

            //spAttributes->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND, (UINT64)videoWindowHandle);

            const DWORD flags = MF_MEDIA_ENGINE_WAITFORSTABLE_STATE;
            spFactory->CreateInstance(flags, spAttributes, &m_spMediaEngine);

            m_spMediaEngine->QueryInterface(__uuidof(IMFMediaEngine), (void**)&m_spEngineEx);

            m_spEngineEx->SetAutoPlay(TRUE);
            m_spEngineEx->SetPreload(MF_MEDIA_ENGINE_PRELOAD_AUTOMATIC);
            m_spEngineEx->SetLoop(TRUE);
            m_spEngineEx->SetVolume(audio);
            m_spEngineEx->SetSource(file);
            //m_spEngineEx->SetDefaultPlaybackRate(3);
            m_spEngineEx->Load();

            swapChain->Present(1, 0);

            LeaveCriticalSection(&critSec);

            cout << "VIDEO STARTED" << endl << "PRESS ENTER TO END VIDEO PLAYBACK." << endl;

            //IDXGIFactory1* idFactory;
            //CreateDXGIFactory1(IID_PPV_ARGS(&idFactory));

            //IDXGIAdapter* spAdapter;
            //idFactory->EnumAdapters(0, &spAdapter);

            //spAdapter->EnumOutputs(0, &spOutput);

            thread playVid = thread(drawToWindow);

            _getch();

            //std::this_thread::sleep_for(std::chrono::seconds(16));

            play = FALSE;
            playVid.join();
            DeleteCriticalSection(&critSec);

            DestroyWindow(videoWindowHandle);
            deviceManager->UnlockDevice(deviceHandle, 1);
            deviceManager->CloseDeviceHandle(deviceHandle);
            m_spEngineEx->Shutdown();
            m_spMediaEngine->Shutdown();
            m_spEngineEx->Release();
            m_spMediaEngine->Release();
            swapChain->Release();
            device->Release();
            deviceManager->Release();
            MFShutdown();
            CoUninitialize();
        }
        else
        {
            cout << "ERROR INITIALIZING COM LIBRARY. :(";
        }
    }
    else
    {
        cout << "INVALID FILE" << endl;
    }
}

RECT rc = { 0 };
LONGLONG pts;
thread transfer, pres;
void transferFrame();
void present();
int complete1 = 0, complete2 = 0;

void drawToWindow()
{
    //backgroundColor = { 203, 192, 255, 255 };

    while (play == true)
    {
        EnterCriticalSection(&critSec);

        if (m_spEngineEx->OnVideoStreamTick(&pts) == S_OK && play == TRUE)
        {
            LeaveCriticalSection(&critSec);

            //x = (VIDEO_WIDTH / cols);
            //y = (VIDEO_HEIGHT / rows);

            //y = (VIDEO_HEIGHT / cols);
            //x = y * conversionWH;

            if (spTextureDst)
            {
                for (int i = 0; i < rows; i++)
                {
                    for (int b = 0; b < cols; b++)
                    {
                        try
                        {
                            if (play == TRUE)
                            {
                                rc = { x * b, y * i, x * b + x, y * i + y };
                                //cout << "3";
                                //m_spEngineEx->TransferVideoFrame(spTextureDst, NULL, &rc, &backgroundColor);
                                //cout << "4";
                                if (multi == 1)
                                {
                                    transfer = thread(transferFrame);
                                    transfer.join();
                                    //transfer.detach();

                                    //std::this_thread::sleep_for(std::chrono::nanoseconds(delay * 2));

                                    //std::this_thread::sleep_for(std::chrono::nanoseconds(delay * 2 + 2));
                                }
                                else
                                {
                                    //std::this_thread::sleep_for(std::chrono::nanoseconds(delay));
                                    //cout << "3";
                                    transferFrame();
                                }
                            }
                        }
                        catch (...)
                        {
                            cout << "FRAME WAITING ERROR, PROCESSOR PROBLEM." << endl;
                        }

                    }
                }

                try
                {
                    if (play == TRUE)
                    {
                        //std::this_thread::sleep_for(std::chrono::nanoseconds(80));
                        //swapChain->Present(1, 0);
                        //spTextureDst->Release();
                        //spTextureDst = nullptr;
                        //cout << "6";
                        if (multi == 1)
                        {
                            pres = thread(present);
                            pres.join();
                            //pres.detach();

                            //std::this_thread::sleep_for(std::chrono::nanoseconds(10));

                            //std::this_thread::sleep_for(std::chrono::nanoseconds(2));
                        }
                        else
                        {
                            present();
                        }
                    }
                }
                catch (...)
                {
                    cout << "PRESENT ERROR." << endl;
                }
            }
            else
            {
                EnterCriticalSection(&critSec);

                swapChain->GetBuffer(0, IID_PPV_ARGS(&spTextureDst));

                LeaveCriticalSection(&critSec);
            }

            //std::this_thread::sleep_for(std::chrono::milliseconds(12));

            //cout << "TRIED";
        }
        else
        {
            LeaveCriticalSection(&critSec);

            //cout << "FAILED";
            std::this_thread::sleep_for(std::chrono::nanoseconds(delay));
        }
    }

    return;
}

void present()
{
    EnterCriticalSection(&critSec);

    std::this_thread::sleep_for(std::chrono::nanoseconds(5));

    swapChain->Present(1, 0);
    //complete2 = 1;

    std::this_thread::sleep_for(std::chrono::nanoseconds(5));

    LeaveCriticalSection(&critSec);
}

void transferFrame()
{
    EnterCriticalSection(&critSec);

    std::this_thread::sleep_for(std::chrono::nanoseconds(delay));
    //std::this_thread::sleep_for(std::chrono::nanoseconds(delay));
    //cout << "3";
    m_spEngineEx->TransferVideoFrame(spTextureDst, NULL, &rc, &backgroundColor);
    //complete1 = 1;
    //cout << "4";
    //std::this_thread::sleep_for(std::chrono::nanoseconds(delay));
    std::this_thread::sleep_for(std::chrono::nanoseconds(delay));

    LeaveCriticalSection(&critSec);
}

HWND FindDTWindow()
{
    HWND hWnd = ::FindWindow(L"Progman", L"Program Manager");
    PDWORD_PTR dResult = 0;

    SendMessageTimeout(hWnd,
        0x052C,
        0,
        NULL,
        SMTO_NORMAL,
        1000,
        dResult);

    HWND hwndWorkW = NULL;
    do
    {
        hwndWorkW = ::FindWindowEx(NULL, hwndWorkW, L"WorkerW", NULL);
        if (NULL == hwndWorkW)
        {
            continue;
        }

        HWND hView = ::FindWindowEx(hwndWorkW, NULL, L"SHELLDLL_DefView", NULL);
        if (NULL == hView)
        {
            continue;
        }

        HWND h = ::FindWindowEx(NULL, hwndWorkW, L"WorkerW", NULL);
        while (NULL != h)
        {
            SendMessage(h, WM_CLOSE, 0, 0);
            h = ::FindWindowEx(NULL, hwndWorkW, L"WorkerW", NULL);
        }
        break;

    } while (true);

    return hWnd;
}

DWORD InitializeWindow(LPVOID tP)
{
    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;

    if (RegisterClass(&wc))
    {
        videoWindowHandle = CreateWindow(
            CLASS_NAME,
            WINDOW_NAME,
            WS_POPUP | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            VIDEO_WIDTH,
            VIDEO_HEIGHT + extraY,
            NULL,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );

        if (videoWindowHandle)
        {
            HDC color = GetDC(videoWindowHandle);
            SetBkColor(color, rgbBlack);
            ShowWindow(videoWindowHandle, SW_SHOWDEFAULT);
            MSG msg = { 0 };
            int first = 1;

            while (true)
            {
                if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);

                    if (!IsWindow(DesktopWindow))
                    {
                        ShowWindow(videoWindowHandle, SW_HIDE);
                        DesktopWindow = FindDTWindow();

                        if (IsWindow(DesktopWindow))
                        {
                            first = 1;
                        }
                    }

                    if (first == 1 && play == TRUE)
                    {
                        SetParent(videoWindowHandle, DesktopWindow);
                        SetWindowPos(videoWindowHandle, HWND_NOTOPMOST, 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT + extraY, SWP_SHOWWINDOW);
                        ShowWindow(videoWindowHandle, SW_SHOWDEFAULT);
                        first = 0;
                    }
                }
                else
                {
                    if (!IsWindow(DesktopWindow))
                    {
                        ShowWindow(videoWindowHandle, SW_HIDE);
                        DesktopWindow = FindDTWindow();

                        if (IsWindow(DesktopWindow))
                        {
                            first = 1;
                        }
                    }
                    else
                    {
                        if (first == 1 && play == TRUE)
                        {
                            SetParent(videoWindowHandle, DesktopWindow);
                            SetWindowPos(videoWindowHandle, HWND_NOTOPMOST, 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT + extraY, SWP_SHOWWINDOW);
                            ShowWindow(videoWindowHandle, SW_SHOWDEFAULT);
                            first = 0;
                        }
                    }

                    if (swapChain && m_spEngineEx && play == TRUE)
                    {
                        m_spEngineEx->GetVideoAspectRatio(&w, &h);
                        conversionHW = (double)h / (double)w;
                        conversionWH = (double)w / (double)h;

                        x = (VIDEO_WIDTH / cols);
                        y = (int)((double)x * conversionHW);
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
        }
    }

    return 0;
}