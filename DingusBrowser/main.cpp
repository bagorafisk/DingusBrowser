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
#include <Uxtheme.h>
#include <vssym32.h>
#include <dwmapi.h>
#include <gdiplus.h>

#define UNICODE
#define _UNICODE

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
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

constexpr int ICON_SIZE = 20;
constexpr COLORREF ICON_COLOR = RGB(95, 99, 104);
constexpr COLORREF ICON_HOVER_COLOR = RGB(32, 33, 36);

HWND g_hwnd = nullptr;
HWND g_urlBar = nullptr;
HWND g_tabControl = nullptr;
HWND g_toolbar = nullptr;

namespace Colors {
	const COLORREF BackgroundColor = RGB(245, 246, 247);
	const COLORREF AccentColor = RGB(66, 133, 244);
	const COLORREF TextColor = RGB(32, 33, 36);
	const COLORREF BorderColor = RGB(218, 220, 224);
	const COLORREF HoverColor = RGB(241, 243, 244);
	const COLORREF ActiveTabColor = RGB(255, 255, 255);
	const COLORREF InactiveTabColor = RGB(241, 243, 244);
}

struct IconPath {
	std::vector<Gdiplus::PointF> points;
	std::vector<BYTE> types;
};

struct WindowStyle {
	static void ApplyModernStyle(HWND hwnd) {
		SetWindowTheme(hwnd, L"Explorer", nullptr);

		DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
		DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));

		SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)CreateSolidBrush(Colors::BackgroundColor));
	}
};

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

std::map<int, IconPath> g_iconPaths;
UINT_PTR g_toolbarHoverTimer = 0;
int g_hoveredButton = -1;

IconPath ParseSVGPath(const wchar_t* pathData) {
	IconPath result;

	// Simple SVG path parser for M, L, and Z commands
	float currentX = 0, currentY = 0;
	const wchar_t* p = pathData;

	while (*p) {
		while (iswspace(*p)) p++;

		if (*p == 'M' || *p == 'm') {
			bool relative = (*p == 'm');
			p++;
			float x = wcstof(p, const_cast<wchar_t**>(&p));
			while (iswspace(*p) || *p == ',') p++;
			float y = wcstof(p, const_cast<wchar_t**>(&p));

			if (relative) {
				x += currentX;
				y += currentY;
			}

			result.points.push_back(Gdiplus::PointF(x, y));
			result.types.push_back(Gdiplus::PathPointTypeStart);
			currentX = x;
			currentY = y;
		}
		else if (*p == 'L' || *p == 'l') {
			bool relative = (*p == 'l');
			p++;
			float x = wcstof(p, const_cast<wchar_t**>(&p));
			while (iswspace(*p) || *p == ',') p++;
			float y = wcstof(p, const_cast<wchar_t**>(&p));

			if (relative) {
				x += currentX;
				y += currentY;
			}

			result.points.push_back(Gdiplus::PointF(x, y));
			result.types.push_back(Gdiplus::PathPointTypeLine);
			currentX = x;
			currentY = y;
		}
		else if (*p == 'Z' || *p == 'z') {
			result.types.back() |= Gdiplus::PathPointTypeCloseSubpath;
			p++;
		}
		else {
			p++;
		}
	}

	return result;
}

void InitializeIconPaths() {
	g_iconPaths[ID_BACK] = ParseSVGPath(L"M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z");
	g_iconPaths[ID_FORWARD] = ParseSVGPath(L"M12 4l-1.41 1.41L16.17 11H4v2h12.17l-5.58 5.59L12 20l8-8z");
	g_iconPaths[ID_REFRESH] = ParseSVGPath(L"M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z");
	g_iconPaths[ID_HOME] = ParseSVGPath(L"M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z");
}

void DrawIcon(HDC hdc, const IconPath& path, int x, int y, COLORREF color) {
	Gdiplus::Graphics graphics(hdc);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

	// Scale the icon to fit our desired size
	float scale = static_cast<float>(ICON_SIZE) / 24.0f;
	Gdiplus::Matrix matrix;
	matrix.Scale(scale, scale);
	matrix.Translate(static_cast<float>(x), static_cast<float>(y));
	graphics.SetTransform(&matrix);

	// Create the path
	Gdiplus::GraphicsPath iconPath;
	iconPath.AddLines(reinterpret_cast<const Gdiplus::PointF*>(path.points.data()), static_cast<INT>(path.points.size()));

	// Draw the icon
	Gdiplus::SolidBrush brush(Gdiplus::Color(GetRValue(color),
		GetGValue(color),
		GetBValue(color)));
	graphics.FillPath(&brush, &iconPath);
}


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
void DrawModernUrlBar(HWND hwnd);
void DrawModerTab(HWND hwnd, HDC hdc, const RECT& rect, bool isSelected);
void HandleUrlBarInput();

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

void DrawModernUrlBar(HWND hwnd) {
	RECT rect;
	GetClientRect(hwnd, &rect);
	HDC hdc = GetDC(hwnd);

	// Create memory DC for double buffering
	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
	HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

	// Fill background
	HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
	FillRect(memDC, &rect, bgBrush);
	DeleteObject(bgBrush);

	// Draw rounded rectangle border
	int radius = 4;
	HPEN borderPen = CreatePen(PS_SOLID, 1, Colors::BorderColor);
	HGDIOBJ oldPen = SelectObject(memDC, borderPen);

	// Draw the rounded rectangle
	RoundRect(memDC, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);

	// Draw the URL text
	SetBkMode(memDC, TRANSPARENT);
	RECT textRect = rect;
	textRect.left += 8; // Text padding
	textRect.right -= 8;

	wchar_t text[2048];
	GetWindowTextW(hwnd, text, 2048);
	DrawTextW(memDC, text, -1, &textRect, DT_VCENTER | DT_SINGLELINE);

	// Copy from memory DC to window DC
	BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

	// Clean up
	SelectObject(memDC, oldPen);
	SelectObject(memDC, oldBitmap);
	DeleteObject(borderPen);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
	ReleaseDC(hwnd, hdc);
}

void DrawModerTab(HWND hwnd, HDC hdc, const RECT& rect, bool isSelected) {
	HBRUSH hBrush = CreateSolidBrush(isSelected ? Colors::ActiveTabColor : Colors::InactiveTabColor);

	FillRect(hdc, &rect, hBrush);
	DeleteObject(hBrush);

	if (!isSelected) {
		HPEN hPen = CreatePen(PS_SOLID, 1, Colors::BorderColor);
		SelectObject(hdc, hPen);
		MoveToEx(hdc, rect.right - 1, rect.top + 4, nullptr);
		LineTo(hdc, rect.right - 1, rect.bottom - 4);
		DeleteObject(hPen);
	}

	if (isSelected) {
		RECT borderRect = rect;
		borderRect.top = borderRect.bottom - 2;
		hBrush = CreateSolidBrush(Colors::AccentColor);
		FillRect(hdc, &borderRect, hBrush);
		DeleteObject(hBrush);
	}
}

void InitializeToolbar(HWND hwnd, HINSTANCE hInstance) {
	// Initialize GDI+
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	// Initialize icon paths
	InitializeIconPaths();

	// Create toolbar with modern style
	g_toolbar = CreateWindowEx(
		0,
		TOOLBARCLASSNAME,
		nullptr,
		WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NORESIZE,
		0, 0, 0, 0,
		hwnd,
		nullptr,
		hInstance,
		nullptr
	);

	// Set modern toolbar styles
	SendMessage(g_toolbar, TB_SETEXTENDEDSTYLE, 0,
		TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS);

	// Create buttons
	TBBUTTON buttons[] = {
		{0, ID_BACK, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Back"},
		{1, ID_FORWARD, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Forward"},
		{2, ID_REFRESH, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Refresh"},
		{3, ID_HOME, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Home"}
	};

	SendMessage(g_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessage(g_toolbar, TB_ADDBUTTONS, _countof(buttons), (LPARAM)buttons);

	// Subclass the toolbar for custom drawing
	SetWindowSubclass(g_toolbar, [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
			switch (uMsg) {
			case WM_PAINT: {
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hwnd, &ps);

				// Create memory DC for double buffering
				HDC memDC = CreateCompatibleDC(hdc);
				HBITMAP memBitmap = CreateCompatibleBitmap(hdc, ps.rcPaint.right, ps.rcPaint.bottom);
				HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

				// Fill background
				RECT rect;
				GetClientRect(hwnd, &rect);
				FillRect(memDC, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));

				// Draw each button
				TBBUTTON button;
				int buttonCount = SendMessage(hwnd, TB_BUTTONCOUNT, 0, 0);
				for (int i = 0; i < buttonCount; i++) {
					SendMessage(hwnd, TB_GETBUTTON, i, (LPARAM)&button);
					RECT buttonRect;
					SendMessage(hwnd, TB_GETITEMRECT, i, (LPARAM)&buttonRect);

					// Calculate icon position
					int iconX = buttonRect.left + (buttonRect.right - buttonRect.left - ICON_SIZE) / 2;
					int iconY = buttonRect.top + (buttonRect.bottom - buttonRect.top - ICON_SIZE) / 2;

					// Draw icon
					auto it = g_iconPaths.find(button.idCommand);
					if (it != g_iconPaths.end()) {
						COLORREF iconColor = (i == g_hoveredButton) ? ICON_HOVER_COLOR : ICON_COLOR;
						DrawIcon(memDC, it->second, iconX, iconY, iconColor);
					}
				}

				// Copy from memory DC to window DC
				BitBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, memDC, 0, 0, SRCCOPY);

				// Clean up
				SelectObject(memDC, oldBitmap);
				DeleteObject(memBitmap);
				DeleteDC(memDC);

				EndPaint(hwnd, &ps);
				return 0;
			}

			case WM_MOUSEMOVE: {
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				int hot = SendMessage(hwnd, TB_HITTEST, 0, (LPARAM)&pt);
				if (hot != g_hoveredButton) {
					g_hoveredButton = hot;
					InvalidateRect(hwnd, NULL, FALSE);

					// Start hover timer if not already started
					if (!g_toolbarHoverTimer) {
						g_toolbarHoverTimer = SetTimer(hwnd, 1, 50, NULL);
					}
				}
				break;
			}

			case WM_MOUSELEAVE: {
				if (g_hoveredButton != -1) {
					g_hoveredButton = -1;
					InvalidateRect(hwnd, NULL, FALSE);
				}
				if (g_toolbarHoverTimer) {
					KillTimer(hwnd, g_toolbarHoverTimer);
					g_toolbarHoverTimer = 0;
				}
				break;
			}
			}
			return DefSubclassProc(hwnd, uMsg, wParam, lParam);
		}, 0, 0);
}

void CreateTab() {
	TabInfo newTab;
	g_tabs.push_back(newTab);

	int tabIndex = g_tabs.size() - 1;

	TCITEM tie;
	tie.mask = TCIF_TEXT;
	tie.pszText = (LPSTR)L"New Tab";
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

LRESULT CALLBACK UrlBarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
	switch (uMsg) {
	case WM_KEYDOWN:
		if (wParam == VK_RETURN) {
			HandleUrlBarInput();
			return 0;
		}
		break;

	case WM_PAINT:
		DrawModernUrlBar(hwnd);
		return 0;

	case WM_NCPAINT:
		return 0;
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
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

	// Add some padding for modern look
	const int PADDING = 8;

	// Position the toolbar
	SendMessage(g_toolbar, TB_AUTOSIZE, 0, 0);

	// Position the tab control with padding
	SetWindowPos(g_tabControl, nullptr,
		PADDING, TOOLBAR_HEIGHT,
		bounds.right - (PADDING * 2), TAB_HEIGHT,
		SWP_NOZORDER);

	// Position the URL bar with padding
	SetWindowPos(g_urlBar, nullptr,
		PADDING, TOOLBAR_HEIGHT + TAB_HEIGHT + (PADDING / 2),
		bounds.right - (PADDING * 2), URL_BAR_HEIGHT,
		SWP_NOZORDER);

	// Calculate WebView bounds with padding
	RECT webViewBounds = {
		PADDING,
		TOOLBAR_HEIGHT + TAB_HEIGHT + URL_BAR_HEIGHT + PADDING,
		bounds.right - PADDING,
		bounds.bottom - PADDING
	};

	// Resize the WebView
	g_tabs[g_currentTab].controller->put_Bounds(webViewBounds);
}

void InitializeControls(HWND hwnd, HINSTANCE hInstance) {
	WindowStyle::ApplyModernStyle(hwnd);

	// Create URL bar with modern styling
	g_urlBar = CreateWindowExW(
		0, // Remove WS_EX_CLIENTEDGE for modern look
		L"EDIT",
		L"",
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
		0, TOOLBAR_HEIGHT + TAB_HEIGHT,
		WINDOW_WIDTH, URL_BAR_HEIGHT,
		hwnd,
		(HMENU)ID_URLBAR,
		hInstance,
		nullptr
	);

	HFONT hFont = CreateFontW(
		-14,              // Height
		0,               // Width
		0,               // Escapement
		0,               // Orientation
		FW_NORMAL,       // Weight
		FALSE,           // Italic
		FALSE,          // Underline
		FALSE,          // StrikeOut
		ANSI_CHARSET,   // CharSet
		OUT_DEFAULT_PRECIS,          // OutPrecision
		CLIP_DEFAULT_PRECIS,         // ClipPrecision
		CLEARTYPE_QUALITY,           // Quality
		DEFAULT_PITCH | FF_DONTCARE, // PitchAndFamily
		L"Segoe UI"     // Modern font
	);

	// Create tab control with modern styling
	g_tabControl = CreateWindowExW(
		0,
		WC_TABCONTROLW,
		nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FLATBUTTONS | TCS_BUTTONS,
		0, TOOLBAR_HEIGHT,
		WINDOW_WIDTH, TAB_HEIGHT,
		hwnd,
		(HMENU)ID_TABCTRL,
		hInstance,
		nullptr
	);

	SendMessage(g_urlBar, WM_SETFONT, (WPARAM)hFont, TRUE);
	SendMessage(g_tabControl, WM_SETFONT, (WPARAM)hFont, TRUE);

	// Set up modern tab control styling
	DWORD extendedStyle = TCS_EX_FLATSEPARATORS;
	SendMessage(g_tabControl, TCM_SETEXTENDEDSTYLE, 0, extendedStyle);

	// Set up URL bar event handling with subclassing
	SetWindowSubclass(g_urlBar, UrlBarProc, 0, 0);
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

void HandleUrlBarInput() {
	if (g_currentTab >= 0 && g_currentTab < g_tabs.size()) {
		NavigateToUrl(g_currentTab);
	}
}

void CreateMenuBar(HWND hwnd) {
	HMENU hMenuBar = CreateMenu();
	HMENU hFileMenu = CreatePopupMenu();
	HMENU hBookmarksMenu = CreatePopupMenu();
	HMENU hToolsMenu = CreatePopupMenu();

	// File menu
	AppendMenuW(hFileMenu, MF_STRING, ID_FILE_NEW_TAB, L"New Tab\tCtrl+T");
	AppendMenuW(hFileMenu, MF_STRING, ID_FILE_CLOSE_TAB, L"Close Tab\tCtrl+W");
	AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFileMenu, MF_STRING, ID_FILE_EXIT, L"Exit\tAlt+F4");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"File");

	// Bookmarks menu
	AppendMenuW(hBookmarksMenu, MF_STRING, ID_BOOKMARKS_ADD, L"Add Bookmark\tCtrl+D");
	AppendMenuW(hBookmarksMenu, MF_STRING, ID_BOOKMARKS_VIEW, L"View Bookmarks\tCtrl+B");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hBookmarksMenu, L"Bookmarks");

	// Tools menu
	AppendMenuW(hToolsMenu, MF_STRING, ID_TOOLS_DEVTOOLS, L"Developer Tools\tF12");
	AppendMenuW(hToolsMenu, MF_STRING, ID_TOOLS_DOWNLOADS, L"Downloads\tCtrl+J");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hToolsMenu, L"Tools");

	// Set the menu bar
	SetMenu(hwnd, hMenuBar);

	// Set up accelerator keys
	HACCEL hAccel = CreateAcceleratorTable(
		(LPACCEL)new ACCEL[]{
			{FVIRTKEY | FCONTROL, 'T', ID_FILE_NEW_TAB},
			{FVIRTKEY | FCONTROL, 'W', ID_FILE_CLOSE_TAB},
			{FVIRTKEY | FCONTROL, 'D', ID_BOOKMARKS_ADD},
			{FVIRTKEY | FCONTROL, 'B', ID_BOOKMARKS_VIEW},
			{FVIRTKEY | FCONTROL, 'J', ID_TOOLS_DOWNLOADS},
			{FVIRTKEY, VK_F12, ID_TOOLS_DEVTOOLS}
		},
		6
	);
}