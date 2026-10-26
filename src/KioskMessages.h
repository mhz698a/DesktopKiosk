#pragma once

#include <windows.h>

constexpr UINT WM_KIOSK_UNLOCK = WM_APP + 1;
constexpr UINT WM_KIOSK_LOCK   = WM_APP + 2;
constexpr UINT WM_KIOSK_CLOSE  = WM_APP + 3;
constexpr UINT WM_KIOSK_CENTRAL_RESPONSE = WM_APP + 4;
constexpr UINT WM_KIOSK_SERVER_IP_RECEIVED = WM_APP + 5;
