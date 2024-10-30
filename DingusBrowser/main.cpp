#include <Windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <WebView2.h>
#include <string>
#include <vector>
#include <CommCtrl.h>
#include <map>

#define UNICODE
#define _UNICODE

#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::WRL;

constexpr int WINDOW_WIDTH = 1024;
constexpr int WINDOW_HEIGHT = 1024;
constexpr int URL_BAR_HEIGHT = 30;
constexpr int TOOLBAR_HEIGHT = 30;
constexpr int TAB_HEIGHT = 30;

constexpr int ID_BACK = 1001;
constexpr int ID_FORWARD = 1002;
constexpr int ID_REFRESH = 1003;
constexpr int ID_HOME = 1004;
constexpr int ID_NEW_TAB = 1005;
constexpr int ID_BOOKMARK = 1006;
constexpr int ID_URLBAR = 1007;
constexpr int ID_TABCTRL = 1008;

constexpr int ID_FILE_NEW_TAB = 2001;
constexpr int ID_FILE_CLOSE_TAB = 2002;
constexpr int ID_FILE_EXIT = 2003;
constexpr int ID_BOOKMARKS_ADD = 2004;
constexpr int ID_BOOKMARKS_VIEW = 2005;
constexpr int ID_TOOLS_DEVTOOLS = 2006;
constexpr int ID_TOOLS_DOWNLOADS = 2007; 

HWND g_hwnd = nullptr;
HWND g_urlBar = nullptr;
HWND g_tabControl = nullptr;
HWND g_toolbar = nullptr;

struct WebViewEventTokens {
	EventRegistrationToken navigationCompletedToken;
	EventRegistrationToken titleChangedToken;
};

struct TabInfo {
	ComPtr<ICoreWebView2Controller> controller;
	ComPtr<ICoreWebView2> webView;
	std::wstring title;
	std::wstring url;
	WebViewEventTokens tokens;
};


std::vector<TabInfo> g_tabs;
int g_currentTab = -1;
std::map<std::wstring, std::wstring> g_bookmarks;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void InitializeWebView(int tabIndex);
void ResizeBrowser();
void NavigateToUrl(int tabIndex);
void CreateTab();
void InitializeControls(HWND hwnd, HINSTANCE hInstance);
void CreateMenuBar(HWND hwnd);
void HandleMenuCommand(WPARAM wParam);
void SaveBookmark();
void ShowBookmarks();
void SwitchToTab(int index);
void CloseTab(int index);
void InitializeToolbar(HWND hwnd, HINSTANCE hInstance);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr)) return 1;

	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES;
	InitCommonControlsEx(&icex);

	const wchar_t CLASS_NAME[] = L"BrowserWindow";

	WNDCLASSW wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClassW(&wc);

	g_hwnd = CreateWindowExW(
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

	if (!g_hwnd) return 0;

	CreateMenuBar(g_hwnd);
	InitializeControls(g_hwnd, hInstance);
	InitializeToolbar(g_hwnd, hInstance);

	CreateTab();

	ShowWindow(g_hwnd, nCmdShow);
	UpdateWindow(g_hwnd);

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

void InitializeToolbar(HWND hwnd, HINSTANCE hInstance) {
	g_toolbar = CreateWindowEx(
		0,
		TOOLBARCLASSNAME,
		nullptr,
		WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
		0, 0, 0, 0,
		hwnd,
		nullptr,
		hInstance,
		nullptr
	);

	TBBUTTON buttons[] = {
		{0, ID_BACK, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Back"},
		{1, ID_FORWARD, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Forward"},
		{2, ID_REFRESH, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Refresh"},
		{3, ID_HOME, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Home"}
	};

	SendMessage(g_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessage(g_toolbar, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);
}

void CreateMenuBar(HWND hwnd) {
	HMENU hMenu = CreateMenu();
	HMENU hFileMenu = CreatePopupMenu();
	HMENU hBookmarksMenu = CreatePopupMenu();
	HMENU hToolsMenu = CreatePopupMenu();

	AppendMenu(hFileMenu, MF_STRING, ID_FILE_NEW_TAB, "New Tab");
	AppendMenu(hFileMenu, MF_STRING, ID_FILE_CLOSE_TAB, "Close Tab");
	AppendMenu(hFileMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenu(hFileMenu, MF_STRING, ID_FILE_EXIT, "Exit");

	AppendMenu(hBookmarksMenu, MF_STRING, ID_BOOKMARKS_ADD, "Add Bookmark");
	AppendMenu(hBookmarksMenu, MF_STRING, ID_BOOKMARKS_VIEW, "View Bookmarks");

	AppendMenu(hToolsMenu, MF_STRING, ID_TOOLS_DEVTOOLS, "Dingus Tools");
	AppendMenu(hToolsMenu, MF_STRING, ID_TOOLS_DOWNLOADS, "Downloads");

	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "File");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hBookmarksMenu, "Bookmarks");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hToolsMenu, "Tools");

	SetMenu(hwnd, hMenu);
}

void CreateTab() {
	TabInfo newTab;
	g_tabs.push_back(newTab);

	int tabIndex = g_tabs.size() - 1;
	
	wchar_t tabText[] = L"New Tab";

	TCITEMW tie = {0};
	tie.mask = TCIF_TEXT;
	tie.pszText = tabText;
	TabCtrl_InsertItem(g_tabControl, tabIndex, &tie);

	InitializeWebView(tabIndex);
	SwitchToTab(tabIndex);
}

void SwitchToTab(int index) {
	if (index < 0 || index >= g_tabs.size()) {
		return;
	}

	g_currentTab = index;
	TabCtrl_SetCurSel(g_tabControl, index);

	// Hide all WebViews
	for (int i = 0; i < g_tabs.size(); i++) {
		if (g_tabs[i].controller) {
			g_tabs[i].controller->put_IsVisible(i == index);
		}
	}

	// Update URL bar with current tab's URL
	if (g_urlBar && g_tabs[index].webView) {
		SetWindowText(g_urlBar, (LPCSTR)g_tabs[index].url.c_str());
	}

	// Resize the browser to update the layout
	ResizeBrowser();
}

void SaveBookmark() {
	if (g_currentTab >= 0 && g_currentTab < g_tabs.size()) {
		TabInfo& currentTab = g_tabs[g_currentTab];
		g_bookmarks[currentTab.title] = currentTab.url;
		MessageBoxW(g_hwnd, L"Bookmark added!", L"Success", MB_OK);
	}
}

void ShowBookmarks() {
	std::wstring bookmarksList;
	for (const auto& bookmark : g_bookmarks) {
		bookmarksList += bookmark.first + L"\n" + bookmark.second + L"\n";
	}
	MessageBoxW(g_hwnd, bookmarksList.c_str(), L"Bookmarks", MB_OK);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_DESTROY: {
		while (!g_tabs.empty()) {
			CloseTab(g_tabs.size() - 1);
		}
		CoUninitialize();
		PostQuitMessage(0);
		return 0;
	}

	case WM_SIZE:
		if (g_currentTab >= 0 && g_currentTab < g_tabs.size()) {
			ResizeBrowser();
		}
		return 0;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_BACK:
			if (g_currentTab >= 0 && g_currentTab < g_tabs.size() && g_tabs[g_currentTab].webView)
				g_tabs[g_currentTab].webView->GoBack();
			break;

		case ID_FORWARD:
			if (g_currentTab >= 0 && g_currentTab < g_tabs.size() && g_tabs[g_currentTab].webView)
				g_tabs[g_currentTab].webView->GoForward();
			break;

		case ID_REFRESH:
			if (g_currentTab >= 0 && g_currentTab < g_tabs.size() && g_tabs[g_currentTab].webView)
				g_tabs[g_currentTab].webView->Reload();
			break;

		case ID_HOME:
			if (g_currentTab >= 0 && g_currentTab < g_tabs.size() && g_tabs[g_currentTab].webView)
				g_tabs[g_currentTab].webView->Navigate(L"https://www.google.com");
			break;

		default:
			HandleMenuCommand(wParam);
			break;
		}
		return 0;

	case WM_NOTIFY: {
		LPNMHDR pnmh = (LPNMHDR)lParam;
		if (pnmh->hwndFrom == g_tabControl && pnmh->code == TCN_SELCHANGE) {
			int newIndex = TabCtrl_GetCurSel(g_tabControl);
			if (newIndex >= 0 && newIndex < g_tabs.size()) {
				SwitchToTab(newIndex);
			}
		}
		return 0;
	}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void HandleMenuCommand(WPARAM wParam) {
	switch (LOWORD(wParam)) {
	case ID_FILE_NEW_TAB:
		CreateTab();
		break;

	case ID_FILE_CLOSE_TAB:
		if (g_currentTab >= 0)
			CloseTab(g_currentTab);
		break;

	case ID_FILE_EXIT:
		DestroyWindow(g_hwnd);
		break;

	case ID_BOOKMARKS_ADD:
		SaveBookmark();
		break;

	case ID_BOOKMARKS_VIEW:
		ShowBookmarks();
		break;

	case ID_TOOLS_DEVTOOLS:
		if (g_currentTab >= 0 && g_tabs[g_currentTab].webView)
			g_tabs[g_currentTab].webView->OpenDevToolsWindow();
		break;
	}
}

void CloseTab(int index) {
	if (index < 0 || index >= g_tabs.size()) return;

	// Remove event handlers before closing
	if (g_tabs[index].webView) {
		g_tabs[index].webView->remove_NavigationCompleted(g_tabs[index].tokens.navigationCompletedToken);
		g_tabs[index].webView->remove_DocumentTitleChanged(g_tabs[index].tokens.titleChangedToken);
	}

	// Release WebView2 resources
	g_tabs[index].webView = nullptr;
	g_tabs[index].controller = nullptr;

	TabCtrl_DeleteItem(g_tabControl, index);
	g_tabs.erase(g_tabs.begin() + index);

	if (g_tabs.empty()) {
		g_currentTab = -1;
		CreateTab();
	}
	else {
		g_currentTab = min(index, g_tabs.size() - 1);
		SwitchToTab(g_currentTab);
	}
}

void InitializeWebView(int tabIndex) {
	CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[tabIndex](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
				if (FAILED(result)) {
					MessageBoxW(g_hwnd, L"Failed to create WebView2 environment", L"Error", MB_OK);
					return result;
				}

				env->CreateCoreWebView2Controller(g_hwnd,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[tabIndex](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
							if (FAILED(result)) {
								MessageBoxW(g_hwnd, L"Failed to create WebView2 controller", L"Error", MB_OK);
								return result;
							}

							if (tabIndex >= 0 && tabIndex < g_tabs.size()) {
								g_tabs[tabIndex].controller = controller;
								controller->get_CoreWebView2(&g_tabs[tabIndex].webView);

								if (g_tabs[tabIndex].webView) {
									// Configure WebView settings
									ICoreWebView2Settings* settings;
									g_tabs[tabIndex].webView->get_Settings(&settings);
									if (settings) {
										settings->put_IsScriptEnabled(TRUE);
										settings->put_AreDefaultScriptDialogsEnabled(TRUE);
										settings->put_IsWebMessageEnabled(TRUE);
										settings->Release();  // Release after use
									}

									// Register navigation event handler
									g_tabs[tabIndex].webView->add_NavigationCompleted(
										Callback<ICoreWebView2NavigationCompletedEventHandler>(
											[tabIndex](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
												if (tabIndex >= 0 && tabIndex < g_tabs.size()) {
													wil::unique_cotaskmem_string url;
													sender->get_Source(&url);
													if (url) {
														g_tabs[tabIndex].url = url.get();
														if (tabIndex == g_currentTab && g_urlBar) {
															SetWindowTextW(g_urlBar, url.get());
														}
													}
												}
												return S_OK;
											}).Get(),
												&g_tabs[tabIndex].tokens.navigationCompletedToken);

									// Register document title changed event handler
									g_tabs[tabIndex].webView->add_DocumentTitleChanged(
										Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
											[tabIndex](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
												if (tabIndex >= 0 && tabIndex < g_tabs.size()) {
													wil::unique_cotaskmem_string title;
													sender->get_DocumentTitle(&title);
													if (title) {
														g_tabs[tabIndex].title = title.get();

														// Update tab text safely
														TCITEMW tie = { 0 };
														tie.mask = TCIF_TEXT;
														tie.pszText = const_cast<LPWSTR>(g_tabs[tabIndex].title.c_str());
														tie.cchTextMax = static_cast<int>(g_tabs[tabIndex].title.length());

														if (IsWindow(g_tabControl)) {
															TabCtrl_SetItem(g_tabControl, tabIndex, &tie);
														}
													}
												}
												return S_OK;
											}).Get(),
												&g_tabs[tabIndex].tokens.titleChangedToken);

									// Position the WebView
									RECT bounds;
									GetClientRect(g_hwnd, &bounds);
									controller->put_Bounds(bounds);

									// Navigate to default page
									g_tabs[tabIndex].webView->Navigate(L"https://www.google.com");
								}
							}
							return S_OK;
						}).Get());
				return S_OK;
			}).Get());
}


void ResizeBrowser() {
	if (g_currentTab < 0 || g_currentTab >= g_tabs.size() || !g_tabs[g_currentTab].controller) {
		return;
	}

	RECT bounds;
	GetClientRect(g_hwnd, &bounds);

	// Position the toolbar
	SendMessage(g_toolbar, TB_AUTOSIZE, 0, 0);

	// Position the tab control
	SetWindowPos(g_tabControl, nullptr,
		0, TOOLBAR_HEIGHT,
		bounds.right, TAB_HEIGHT,
		SWP_NOZORDER);

	// Position the URL bar
	SetWindowPos(g_urlBar, nullptr,
		0, TOOLBAR_HEIGHT + TAB_HEIGHT,
		bounds.right, URL_BAR_HEIGHT,
		SWP_NOZORDER);

	// Calculate WebView bounds
	RECT webViewBounds = {
		0,
		TOOLBAR_HEIGHT + TAB_HEIGHT + URL_BAR_HEIGHT,
		bounds.right,
		bounds.bottom
	};

	// Resize the WebView
	g_tabs[g_currentTab].controller->put_Bounds(webViewBounds);
}

void InitializeControls(HWND hwnd, HINSTANCE hInstance) {
	// Create URL bar
	g_urlBar = CreateWindowExW(
		0,
		L"EDIT",
		L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
		0, TOOLBAR_HEIGHT + TAB_HEIGHT,
		WINDOW_WIDTH, URL_BAR_HEIGHT,
		hwnd,
		(HMENU)ID_URLBAR,
		hInstance,
		nullptr
	);

	// Create tab control
	g_tabControl = CreateWindowExW(
		0,
		WC_TABCONTROLW, 
		nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
		0, TOOLBAR_HEIGHT,
		WINDOW_WIDTH, TAB_HEIGHT,
		hwnd,
		(HMENU)ID_TABCTRL,
		hInstance,
		nullptr
	);

	// Set default font for URL bar and tab control
	HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	if (g_urlBar) SendMessage(g_urlBar, WM_SETFONT, (WPARAM)hFont, TRUE);
	if (g_tabControl) SendMessage(g_tabControl, WM_SETFONT, (WPARAM)hFont, TRUE);

	// Set up URL bar event handling
	if (g_urlBar) {
		SetWindowSubclass(g_urlBar, [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
			UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
				if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
					if (g_currentTab >= 0 && g_currentTab < g_tabs.size()) {
						NavigateToUrl(g_currentTab);
					}
					return 0;
				}
				return DefSubclassProc(hwnd, uMsg, wParam, lParam);
			}, 0, 0);
	}
}

void NavigateToUrl(int tabIndex) {
	if (tabIndex < 0 || tabIndex >= g_tabs.size() || !g_tabs[tabIndex].webView) {
		return;
	}

	// Get the URL from the URL bar
	wchar_t urlBuffer[2048];
	GetWindowTextW(g_urlBar, urlBuffer, 2048);
	std::wstring url(urlBuffer);

	// If the URL doesn't start with a protocol, add https://
	if (url.find(L"://") == std::wstring::npos) {
		// Check if it's a valid IP address or localhost
		if (url.find(L"localhost") == 0 ||
			url.find(L"127.0.0.1") == 0) {
			url = L"http://" + url;
		}
		// Check if it's a file path
		else if (url.find(L"/") == 0 ||
			(url.length() > 1 && url[1] == L':')) {
			url = L"file:///" + url;
		}
		// Default to https
		else {
			// Remove any leading/trailing whitespace
			url.erase(0, url.find_first_not_of(L" \t"));
			url.erase(url.find_last_not_of(L" \t") + 1);

			// If it starts with "www.", just add https://
			if (url.substr(0, 4) == L"www.") {
				url = L"https://" + url;
			}
			// Otherwise, add both https:// and www.
			else if (url.find(L".") != std::wstring::npos) {
				url = L"https://www." + url;
			}
			// If it doesn't contain a dot, treat it as a search query
			else {
				url = L"https://www.google.com/search?q=" +
					std::wstring(urlBuffer);
			}
		}
	}

	// Update the URL bar with the processed URL
	SetWindowTextW(g_urlBar, url.c_str());

	// Navigate to the URL
	HRESULT hr = g_tabs[tabIndex].webView->Navigate(url.c_str());
	if (FAILED(hr)) {
		MessageBoxW(g_hwnd,
			L"Failed to navigate to the specified URL",
			L"Navigation Error",
			MB_OK | MB_ICONERROR);
	}
}