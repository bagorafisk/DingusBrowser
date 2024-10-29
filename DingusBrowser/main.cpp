#include <Windows.h>
#include <wrl.h>
#include "wil/com.h"
#include <WebView2.h>
#include <string>

using namespace Microsoft::WRL;

constexpr int WINDOW_WIDTH = 1024;
constexpr int WINDOW_HEIGHT = 1024;
constexpr int URL_BAR_HEIGHT = 30;

HWND g_hwnd = nullptr;
HWND g_urlBar = nullptr;
ComPtr<ICoreWebView2Controller> g_webViewController;
ComPtr<ICoreWebView2> g_webView;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void InitializeWebView();
void ResizeBrowser();
void NavigateToUrl();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	const wchar_t CLASS_NAME[] = L"BrowserWindow";
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;

	RegisterClass(&wc);

	g_hwnd = CreateWindowEx(
		0,
		CLASS_NAME,
		L"DINGUS BROWSER",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	g_urlBar = CreateWindow(
		L"EDIT",
		L"https://www.google.com",
		WS_CHILD | WS_VISIBLE | WS_BORDER,
		0, 0,
		WINDOW_WIDTH, URL_BAR_HEIGHT,
		g_hwnd,
		nullptr,
		hInstance,
		nullptr
	);

	if (g_hwnd == nullptr) {
		return 0;
	}

	ShowWindow(g_hwnd, nCmdShow);
	UpdateWindow(g_hwnd);

	InitializeWebView();

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_SIZE:
		if (g_webViewController) {
			ResizeBrowser();
		}
		return 0;
	
	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			NavigateToUrl();
		}
		return 0;
	
	case WM_CHAR:
		if (wParam == VK_RETURN) {
			NavigateToUrl();
		}
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void InitializeWebView() {
	// Create WebView2 Environment
	CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {

				// Create WebView2
				env->CreateCoreWebView2Controller(g_hwnd,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
							if (controller) {
								g_webViewController = controller;
								g_webViewController->get_CoreWebView2(&g_webView);

								// Initial navigation
								g_webView->Navigate(L"https://www.google.com");

								ResizeBrowser();
							}
							return S_OK;
						}).Get());
				return S_OK;
			}).Get());
}

void ResizeBrowser() {
	RECT bounds;
	GetClientRect(g_hwnd, &bounds);

	// Adjust URL bar size
	SetWindowPos(g_urlBar, nullptr,
		0, 0,
		bounds.right, URL_BAR_HEIGHT,
		SWP_NOZORDER);

	// Adjust WebView size
	g_webViewController->put_Bounds({
		0,
		URL_BAR_HEIGHT,
		bounds.right,
		bounds.bottom
		});
}

void NavigateToUrl() {
	wchar_t url[1024];
	GetWindowText(g_urlBar, url, 1024);

	std::wstring urlStr(url);
	if (urlStr.find(L"http://") != 0 && urlStr.find(L"https://") != 0) {
		urlStr = L"https://" + urlStr;
	}

	if (g_webView) {
		g_webView->Navigate(urlStr.c_str());
	}
}