/*
 * 16-bit messaging support
 *
 * Copyright 2001 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "wine/winuser16.h"
#include "wownt32.h"
#include "winerror.h"
#include "win.h"
#include "user_private.h"
#include "controls.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msg);

DWORD USER16_AlertableWait = 0;

static struct wow_handlers32 wow_handlers32;

static LRESULT cwp_hook_callback( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  LRESULT *result, void *arg )
{
    CWPSTRUCT cwp;

    cwp.hwnd    = hwnd;
    cwp.message = msg;
    cwp.wParam  = wp;
    cwp.lParam  = lp;
    *result = 0;
    return HOOK_CallHooks( WH_CALLWNDPROC, HC_ACTION, 1, (LPARAM)&cwp, FALSE );
}

static LRESULT send_message_callback( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                      LRESULT *result, void *arg )
{
    *result = SendMessageA( hwnd, msg, wp, lp );
    return *result;
}

static LRESULT post_message_callback( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                      LRESULT *result, void *arg )
{
    *result = 0;
    return PostMessageA( hwnd, msg, wp, lp );
}

static LRESULT post_thread_message_callback( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                             LRESULT *result, void *arg )
{
    DWORD_PTR tid = (DWORD_PTR)arg;
    *result = 0;
    return PostThreadMessageA( tid, msg, wp, lp );
}

static LRESULT get_message_callback( HWND16 hwnd, UINT16 msg, WPARAM16 wp, LPARAM lp,
                                     LRESULT *result, void *arg )
{
    MSG16 *msg16 = arg;

    msg16->hwnd    = hwnd;
    msg16->message = msg;
    msg16->wParam  = wp;
    msg16->lParam  = lp;
    *result = 0;
    return 0;
}

static LRESULT defdlg_proc_callback( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     LRESULT *result, void *arg )
{
    *result = DefDlgProcA( hwnd, msg, wp, lp );
    return *result;
}


/***********************************************************************
 *		SendMessage  (USER.111)
 */
LRESULT WINAPI SendMessage16( HWND16 hwnd16, UINT16 msg, WPARAM16 wparam, LPARAM lparam )
{
    LRESULT result;
    HWND hwnd = WIN_Handle32( hwnd16 );

    if (hwnd != HWND_BROADCAST && WIN_IsCurrentThread(hwnd))
    {
        /* call 16-bit window proc directly */
        WNDPROC16 winproc;

        /* first the WH_CALLWNDPROC hook */
        if (HOOK_IsHooked( WH_CALLWNDPROC ))
            WINPROC_CallProc16To32A( cwp_hook_callback, hwnd16, msg, wparam, lparam, &result, NULL );

        if (!(winproc = (WNDPROC16)GetWindowLong16( hwnd16, GWLP_WNDPROC ))) return 0;

        SPY_EnterMessage( SPY_SENDMESSAGE16, hwnd, msg, wparam, lparam );
        result = CallWindowProc16( winproc, hwnd16, msg, wparam, lparam );
        SPY_ExitMessage( SPY_RESULT_OK16, hwnd, msg, result, wparam, lparam );
    }
    else  /* map to 32-bit unicode for inter-thread/process message */
    {
        WINPROC_CallProc16To32A( send_message_callback, hwnd16, msg, wparam, lparam, &result, NULL );
    }
    return result;
}


/***********************************************************************
 *		PostMessage  (USER.110)
 */
BOOL16 WINAPI PostMessage16( HWND16 hwnd, UINT16 msg, WPARAM16 wparam, LPARAM lparam )
{
    LRESULT unused;
    return WINPROC_CallProc16To32A( post_message_callback, hwnd, msg, wparam, lparam, &unused, NULL );
}


/***********************************************************************
 *		PostAppMessage (USER.116)
 */
BOOL16 WINAPI PostAppMessage16( HTASK16 hTask, UINT16 msg, WPARAM16 wparam, LPARAM lparam )
{
    LRESULT unused;
    DWORD_PTR tid = HTASK_32( hTask );

    if (!tid) return FALSE;
    return WINPROC_CallProc16To32A( post_thread_message_callback, 0, msg, wparam, lparam,
                                    &unused, (void *)tid );
}


/***********************************************************************
 *		InSendMessage  (USER.192)
 */
BOOL16 WINAPI InSendMessage16(void)
{
    return InSendMessage();
}


/***********************************************************************
 *		ReplyMessage  (USER.115)
 */
void WINAPI ReplyMessage16( LRESULT result )
{
    ReplyMessage( result );
}


/***********************************************************************
 *		PeekMessage32 (USER.819)
 */
BOOL16 WINAPI PeekMessage32_16( MSG32_16 *msg16, HWND16 hwnd16,
                                UINT16 first, UINT16 last, UINT16 flags,
                                BOOL16 wHaveParamHigh )
{
    MSG msg;
    LRESULT unused;
    HWND hwnd = WIN_Handle32( hwnd16 );

    if(USER16_AlertableWait)
        MsgWaitForMultipleObjectsEx( 0, NULL, 0, 0, MWMO_ALERTABLE );
    if (!PeekMessageA( &msg, hwnd, first, last, flags )) return FALSE;

    msg16->msg.time    = msg.time;
    msg16->msg.pt.x    = (INT16)msg.pt.x;
    msg16->msg.pt.y    = (INT16)msg.pt.y;
    if (wHaveParamHigh) msg16->wParamHigh = HIWORD(msg.wParam);
    WINPROC_CallProc32ATo16( get_message_callback, msg.hwnd, msg.message, msg.wParam, msg.lParam,
                             &unused, &msg16->msg );
    return TRUE;
}


/***********************************************************************
 *		DefWindowProc (USER.107)
 */
LRESULT WINAPI DefWindowProc16( HWND16 hwnd16, UINT16 msg, WPARAM16 wParam, LPARAM lParam )
{
    LRESULT result;
    HWND hwnd = WIN_Handle32( hwnd16 );

    SPY_EnterMessage( SPY_DEFWNDPROC16, hwnd, msg, wParam, lParam );

    switch(msg)
    {
    case WM_NCCREATE:
        {
            CREATESTRUCT16 *cs16 = MapSL(lParam);
            CREATESTRUCTA cs32;

            cs32.lpCreateParams = ULongToPtr(cs16->lpCreateParams);
            cs32.hInstance      = HINSTANCE_32(cs16->hInstance);
            cs32.hMenu          = HMENU_32(cs16->hMenu);
            cs32.hwndParent     = WIN_Handle32(cs16->hwndParent);
            cs32.cy             = cs16->cy;
            cs32.cx             = cs16->cx;
            cs32.y              = cs16->y;
            cs32.x              = cs16->x;
            cs32.style          = cs16->style;
            cs32.dwExStyle      = cs16->dwExStyle;
            cs32.lpszName       = MapSL(cs16->lpszName);
            cs32.lpszClass      = MapSL(cs16->lpszClass);
            result = DefWindowProcA( hwnd, msg, wParam, (LPARAM)&cs32 );
        }
        break;

    case WM_NCCALCSIZE:
        {
            RECT16 *rect16 = MapSL(lParam);
            RECT rect32;

            rect32.left    = rect16->left;
            rect32.top     = rect16->top;
            rect32.right   = rect16->right;
            rect32.bottom  = rect16->bottom;

            result = DefWindowProcA( hwnd, msg, wParam, (LPARAM)&rect32 );

            rect16->left   = rect32.left;
            rect16->top    = rect32.top;
            rect16->right  = rect32.right;
            rect16->bottom = rect32.bottom;
        }
        break;

    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS16 *pos16 = MapSL(lParam);
            WINDOWPOS pos32;

            pos32.hwnd             = WIN_Handle32(pos16->hwnd);
            pos32.hwndInsertAfter  = WIN_Handle32(pos16->hwndInsertAfter);
            pos32.x                = pos16->x;
            pos32.y                = pos16->y;
            pos32.cx               = pos16->cx;
            pos32.cy               = pos16->cy;
            pos32.flags            = pos16->flags;

            result = DefWindowProcA( hwnd, msg, wParam, (LPARAM)&pos32 );

            pos16->hwnd            = HWND_16(pos32.hwnd);
            pos16->hwndInsertAfter = HWND_16(pos32.hwndInsertAfter);
            pos16->x               = pos32.x;
            pos16->y               = pos32.y;
            pos16->cx              = pos32.cx;
            pos16->cy              = pos32.cy;
            pos16->flags           = pos32.flags;
        }
        break;

    case WM_GETTEXT:
    case WM_SETTEXT:
        result = DefWindowProcA( hwnd, msg, wParam, (LPARAM)MapSL(lParam) );
        break;

    default:
        result = DefWindowProcA( hwnd, msg, wParam, lParam );
        break;
    }

    SPY_ExitMessage( SPY_RESULT_DEFWND16, hwnd, msg, result, wParam, lParam );
    return result;
}


/***********************************************************************
 *              DefDlgProc (USER.308)
 */
LRESULT WINAPI DefDlgProc16( HWND16 hwnd, UINT16 msg, WPARAM16 wParam, LPARAM lParam )
{
    LRESULT result;
    WINPROC_CallProc16To32A( defdlg_proc_callback, hwnd, msg, wParam, lParam, &result, 0 );
    return result;
}


/***********************************************************************
 *		PeekMessage  (USER.109)
 */
BOOL16 WINAPI PeekMessage16( MSG16 *msg, HWND16 hwnd,
                             UINT16 first, UINT16 last, UINT16 flags )
{
    return PeekMessage32_16( (MSG32_16 *)msg, hwnd, first, last, flags, FALSE );
}


/***********************************************************************
 *		GetMessage32  (USER.820)
 */
BOOL16 WINAPI GetMessage32_16( MSG32_16 *msg16, HWND16 hwnd16, UINT16 first,
                               UINT16 last, BOOL16 wHaveParamHigh )
{
    MSG msg;
    LRESULT unused;
    HWND hwnd = WIN_Handle32( hwnd16 );

    if(USER16_AlertableWait)
        MsgWaitForMultipleObjectsEx( 0, NULL, INFINITE, 0, MWMO_ALERTABLE );
    GetMessageA( &msg, hwnd, first, last );
    msg16->msg.time    = msg.time;
    msg16->msg.pt.x    = (INT16)msg.pt.x;
    msg16->msg.pt.y    = (INT16)msg.pt.y;
    if (wHaveParamHigh) msg16->wParamHigh = HIWORD(msg.wParam);
    WINPROC_CallProc32ATo16( get_message_callback, msg.hwnd, msg.message, msg.wParam, msg.lParam,
                             &unused, &msg16->msg );

    TRACE( "message %04x, hwnd %p, filter(%04x - %04x)\n",
           msg16->msg.message, hwnd, first, last );

    return msg16->msg.message != WM_QUIT;
}


/***********************************************************************
 *		GetMessage  (USER.108)
 */
BOOL16 WINAPI GetMessage16( MSG16 *msg, HWND16 hwnd, UINT16 first, UINT16 last )
{
    return GetMessage32_16( (MSG32_16 *)msg, hwnd, first, last, FALSE );
}


/***********************************************************************
 *		TranslateMessage32 (USER.821)
 */
BOOL16 WINAPI TranslateMessage32_16( const MSG32_16 *msg, BOOL16 wHaveParamHigh )
{
    MSG msg32;

    msg32.hwnd    = WIN_Handle32( msg->msg.hwnd );
    msg32.message = msg->msg.message;
    msg32.wParam  = MAKEWPARAM( msg->msg.wParam, wHaveParamHigh ? msg->wParamHigh : 0 );
    msg32.lParam  = msg->msg.lParam;
    return TranslateMessage( &msg32 );
}


/***********************************************************************
 *		TranslateMessage (USER.113)
 */
BOOL16 WINAPI TranslateMessage16( const MSG16 *msg )
{
    return TranslateMessage32_16( (const MSG32_16 *)msg, FALSE );
}


/***********************************************************************
 *		DispatchMessage (USER.114)
 */
LONG WINAPI DispatchMessage16( const MSG16* msg )
{
    WND * wndPtr;
    WNDPROC16 winproc;
    LONG retval;
    HWND hwnd = WIN_Handle32( msg->hwnd );

      /* Process timer messages */
    if ((msg->message == WM_TIMER) || (msg->message == WM_SYSTIMER))
    {
        if (msg->lParam)
            return CallWindowProc16( (WNDPROC16)msg->lParam, msg->hwnd,
                                     msg->message, msg->wParam, GetTickCount() );
    }

    if (!(wndPtr = WIN_GetPtr( hwnd )))
    {
        if (msg->hwnd) SetLastError( ERROR_INVALID_WINDOW_HANDLE );
        return 0;
    }
    if (wndPtr == WND_OTHER_PROCESS || wndPtr == WND_DESKTOP)
    {
        if (IsWindow( hwnd )) SetLastError( ERROR_MESSAGE_SYNC_ONLY );
        else SetLastError( ERROR_INVALID_WINDOW_HANDLE );
        return 0;
    }
    winproc = WINPROC_GetProc16( wndPtr->winproc, wndPtr->flags & WIN_ISUNICODE );
    WIN_ReleasePtr( wndPtr );

    SPY_EnterMessage( SPY_DISPATCHMESSAGE16, hwnd, msg->message, msg->wParam, msg->lParam );
    retval = CallWindowProc16( winproc, msg->hwnd, msg->message, msg->wParam, msg->lParam );
    SPY_ExitMessage( SPY_RESULT_OK16, hwnd, msg->message, retval, msg->wParam, msg->lParam );

    return retval;
}


/***********************************************************************
 *		DispatchMessage32 (USER.822)
 */
LONG WINAPI DispatchMessage32_16( const MSG32_16 *msg16, BOOL16 wHaveParamHigh )
{
    if (wHaveParamHigh == FALSE)
        return DispatchMessage16( &msg16->msg );
    else
    {
        MSG msg;

        msg.hwnd    = WIN_Handle32( msg16->msg.hwnd );
        msg.message = msg16->msg.message;
        msg.wParam  = MAKEWPARAM( msg16->msg.wParam, msg16->wParamHigh );
        msg.lParam  = msg16->msg.lParam;
        msg.time    = msg16->msg.time;
        msg.pt.x    = msg16->msg.pt.x;
        msg.pt.y    = msg16->msg.pt.y;
        return DispatchMessageA( &msg );
    }
}


/***********************************************************************
 *		IsDialogMessage (USER.90)
 */
BOOL16 WINAPI IsDialogMessage16( HWND16 hwndDlg, MSG16 *msg16 )
{
    MSG msg;
    HWND hwndDlg32;

    msg.hwnd  = WIN_Handle32(msg16->hwnd);
    hwndDlg32 = WIN_Handle32(hwndDlg);

    switch(msg16->message)
    {
    case WM_KEYDOWN:
    case WM_CHAR:
    case WM_SYSCHAR:
        msg.message = msg16->message;
        msg.wParam  = msg16->wParam;
        msg.lParam  = msg16->lParam;
        return IsDialogMessageA( hwndDlg32, &msg );
    }

    if ((hwndDlg32 != msg.hwnd) && !IsChild( hwndDlg32, msg.hwnd )) return FALSE;
    TranslateMessage16( msg16 );
    DispatchMessage16( msg16 );
    return TRUE;
}


/***********************************************************************
 *		MsgWaitForMultipleObjects  (USER.640)
 */
DWORD WINAPI MsgWaitForMultipleObjects16( DWORD count, CONST HANDLE *handles,
                                          BOOL wait_all, DWORD timeout, DWORD mask )
{
    return MsgWaitForMultipleObjectsEx( count, handles, timeout, mask,
                                        wait_all ? MWMO_WAITALL : 0 );
}


/**********************************************************************
 *		SetDoubleClickTime (USER.20)
 */
void WINAPI SetDoubleClickTime16( UINT16 interval )
{
    SetDoubleClickTime( interval );
}


/**********************************************************************
 *		GetDoubleClickTime (USER.21)
 */
UINT16 WINAPI GetDoubleClickTime16(void)
{
    return GetDoubleClickTime();
}


/***********************************************************************
 *		PostQuitMessage (USER.6)
 */
void WINAPI PostQuitMessage16( INT16 exitCode )
{
    PostQuitMessage( exitCode );
}


/**********************************************************************
 *		GetKeyState (USER.106)
 */
INT16 WINAPI GetKeyState16(INT16 vkey)
{
    return GetKeyState(vkey);
}


/**********************************************************************
 *		GetKeyboardState (USER.222)
 */
BOOL WINAPI GetKeyboardState16( LPBYTE state )
{
    return GetKeyboardState( state );
}


/**********************************************************************
 *		SetKeyboardState (USER.223)
 */
BOOL WINAPI SetKeyboardState16( LPBYTE state )
{
    return SetKeyboardState( state );
}


/***********************************************************************
 *		SetMessageQueue (USER.266)
 */
BOOL16 WINAPI SetMessageQueue16( INT16 size )
{
    return SetMessageQueue( size );
}


/***********************************************************************
 *		UserYield (USER.332)
 */
void WINAPI UserYield16(void)
{
    MSG msg;
    PeekMessageW( &msg, 0, 0, 0, PM_REMOVE | PM_QS_SENDMESSAGE );
}


/***********************************************************************
 *		GetQueueStatus (USER.334)
 */
DWORD WINAPI GetQueueStatus16( UINT16 flags )
{
    return GetQueueStatus( flags );
}


/***********************************************************************
 *		GetInputState (USER.335)
 */
BOOL16 WINAPI GetInputState16(void)
{
    return GetInputState();
}


/**********************************************************************
 *           TranslateAccelerator      (USER.178)
 */
INT16 WINAPI TranslateAccelerator16( HWND16 hwnd, HACCEL16 hAccel, LPMSG16 msg )
{
    MSG msg32;

    if (!msg) return 0;
    msg32.message = msg->message;
    /* msg32.hwnd not used */
    msg32.wParam  = msg->wParam;
    msg32.lParam  = msg->lParam;
    return TranslateAcceleratorW( WIN_Handle32(hwnd), HACCEL_32(hAccel), &msg32 );
}


/**********************************************************************
 *		TranslateMDISysAccel (USER.451)
 */
BOOL16 WINAPI TranslateMDISysAccel16( HWND16 hwndClient, LPMSG16 msg )
{
    if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN)
    {
        MSG msg32;
        msg32.hwnd    = WIN_Handle32(msg->hwnd);
        msg32.message = msg->message;
        msg32.wParam  = msg->wParam;
        msg32.lParam  = msg->lParam;
        /* MDICLIENTINFO is still the same for win32 and win16 ... */
        return TranslateMDISysAccel( WIN_Handle32(hwndClient), &msg32 );
    }
    return 0;
}


/***********************************************************************
 *           button_proc16
 */
static LRESULT button_proc16( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, BOOL unicode )
{
    static const UINT msg16_offset = BM_GETCHECK16 - BM_GETCHECK;

    switch (msg)
    {
    case BM_GETCHECK16:
    case BM_SETCHECK16:
    case BM_GETSTATE16:
    case BM_SETSTATE16:
    case BM_SETSTYLE16:
        return wow_handlers32.button_proc( hwnd, msg - msg16_offset, wParam, lParam, FALSE );
    default:
        return wow_handlers32.button_proc( hwnd, msg, wParam, lParam, unicode );
    }
}


/***********************************************************************
 *           combo_proc16
 */
static LRESULT combo_proc16( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, BOOL unicode )
{
    static const UINT msg16_offset = CB_GETEDITSEL16 - CB_GETEDITSEL;

    switch (msg)
    {
    case CB_INSERTSTRING16:
    case CB_SELECTSTRING16:
    case CB_FINDSTRING16:
    case CB_FINDSTRINGEXACT16:
        wParam = (INT)(INT16)wParam;
        /* fall through */
    case CB_ADDSTRING16:
        if (GetWindowLongW( hwnd, GWL_STYLE ) & CBS_HASSTRINGS) lParam = (LPARAM)MapSL(lParam);
        msg -= msg16_offset;
        break;
    case CB_SETITEMHEIGHT16:
    case CB_GETITEMHEIGHT16:
    case CB_SETCURSEL16:
    case CB_GETLBTEXTLEN16:
    case CB_GETITEMDATA16:
    case CB_SETITEMDATA16:
        wParam = (INT)(INT16)wParam;	/* signed integer */
        msg -= msg16_offset;
        break;
    case CB_GETDROPPEDCONTROLRECT16:
        lParam = (LPARAM)MapSL(lParam);
        if (lParam)
        {
            RECT r;
            RECT16 *r16 = (RECT16 *)lParam;
            wow_handlers32.combo_proc( hwnd, CB_GETDROPPEDCONTROLRECT, wParam, (LPARAM)&r, FALSE );
            r16->left   = r.left;
            r16->top    = r.top;
            r16->right  = r.right;
            r16->bottom = r.bottom;
        }
        return CB_OKAY;
    case CB_DIR16:
        if (wParam & DDL_DRIVES) wParam |= DDL_EXCLUSIVE;
        lParam = (LPARAM)MapSL(lParam);
        msg -= msg16_offset;
        break;
    case CB_GETLBTEXT16:
        wParam = (INT)(INT16)wParam;
        lParam = (LPARAM)MapSL(lParam);
        msg -= msg16_offset;
        break;
    case CB_GETEDITSEL16:
        wParam = lParam = 0;   /* just in case */
        msg -= msg16_offset;
        break;
    case CB_LIMITTEXT16:
    case CB_SETEDITSEL16:
    case CB_DELETESTRING16:
    case CB_RESETCONTENT16:
    case CB_GETDROPPEDSTATE16:
    case CB_SHOWDROPDOWN16:
    case CB_GETCOUNT16:
    case CB_GETCURSEL16:
    case CB_SETEXTENDEDUI16:
    case CB_GETEXTENDEDUI16:
        msg -= msg16_offset;
        break;
    default:
        return wow_handlers32.combo_proc( hwnd, msg, wParam, lParam, unicode );
    }
    return wow_handlers32.combo_proc( hwnd, msg, wParam, lParam, FALSE );
}


/***********************************************************************
 *           listbox_proc16
 */
static LRESULT listbox_proc16( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, BOOL unicode )
{
    static const UINT msg16_offset = LB_ADDSTRING16 - LB_ADDSTRING;
    LRESULT ret;

    switch (msg)
    {
    case LB_RESETCONTENT16:
    case LB_DELETESTRING16:
    case LB_GETITEMDATA16:
    case LB_SETITEMDATA16:
    case LB_GETCOUNT16:
    case LB_GETTEXTLEN16:
    case LB_GETCURSEL16:
    case LB_GETTOPINDEX16:
    case LB_GETITEMHEIGHT16:
    case LB_SETCARETINDEX16:
    case LB_GETCARETINDEX16:
    case LB_SETTOPINDEX16:
    case LB_SETCOLUMNWIDTH16:
    case LB_GETSELCOUNT16:
    case LB_SELITEMRANGE16:
    case LB_SELITEMRANGEEX16:
    case LB_GETHORIZONTALEXTENT16:
    case LB_SETHORIZONTALEXTENT16:
    case LB_GETANCHORINDEX16:
    case LB_CARETON16:
    case LB_CARETOFF16:
        msg -= msg16_offset;
        break;
    case LB_GETSEL16:
    case LB_SETSEL16:
    case LB_SETCURSEL16:
    case LB_SETANCHORINDEX16:
        wParam = (INT)(INT16)wParam;
        msg -= msg16_offset;
        break;
    case LB_INSERTSTRING16:
    case LB_FINDSTRING16:
    case LB_FINDSTRINGEXACT16:
    case LB_SELECTSTRING16:
        wParam = (INT)(INT16)wParam;
        /* fall through */
    case LB_ADDSTRING16:
    case LB_ADDFILE16:
    {
        DWORD style = GetWindowLongW( hwnd, GWL_STYLE );
        if ((style & LBS_HASSTRINGS) || !(style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)))
            lParam = (LPARAM)MapSL(lParam);
        msg -= msg16_offset;
        break;
    }
    case LB_GETTEXT16:
        lParam = (LPARAM)MapSL(lParam);
        msg -= msg16_offset;
        break;
    case LB_SETITEMHEIGHT16:
        lParam = LOWORD(lParam);
        msg -= msg16_offset;
        break;
    case LB_GETITEMRECT16:
        {
            RECT rect;
            RECT16 *r16 = MapSL(lParam);
            ret = wow_handlers32.listbox_proc( hwnd, LB_GETITEMRECT, (INT16)wParam, (LPARAM)&rect, FALSE );
            r16->left   = rect.left;
            r16->top    = rect.top;
            r16->right  = rect.right;
            r16->bottom = rect.bottom;
            return ret;
        }
    case LB_GETSELITEMS16:
    {
        INT16 *array16 = MapSL( lParam );
        INT i, count = (INT16)wParam, *array;
        if (!(array = HeapAlloc( GetProcessHeap(), 0, wParam * sizeof(*array) ))) return LB_ERRSPACE;
        ret = wow_handlers32.listbox_proc( hwnd, LB_GETSELITEMS, count, (LPARAM)array, FALSE );
        for (i = 0; i < ret; i++) array16[i] = array[i];
        HeapFree( GetProcessHeap(), 0, array );
        return ret;
    }
    case LB_DIR16:
        /* according to Win16 docs, DDL_DRIVES should make DDL_EXCLUSIVE
         * be set automatically (this is different in Win32) */
        if (wParam & DDL_DRIVES) wParam |= DDL_EXCLUSIVE;
        lParam = (LPARAM)MapSL(lParam);
        msg -= msg16_offset;
        break;
    case LB_SETTABSTOPS16:
    {
        INT i, count, *tabs = NULL;
        INT16 *tabs16 = MapSL( lParam );

        if ((count = (INT16)wParam) > 0)
        {
            if (!(tabs = HeapAlloc( GetProcessHeap(), 0, wParam * sizeof(*tabs) ))) return LB_ERRSPACE;
            for (i = 0; i < count; i++) tabs[i] = tabs16[i] << 1; /* FIXME */
        }
        ret = wow_handlers32.listbox_proc( hwnd, LB_SETTABSTOPS, count, (LPARAM)tabs, FALSE );
        HeapFree( GetProcessHeap(), 0, tabs );
        return ret;
    }
    default:
        return wow_handlers32.listbox_proc( hwnd, msg, wParam, lParam, unicode );
    }
    return wow_handlers32.listbox_proc( hwnd, msg, wParam, lParam, FALSE );
}


void register_wow_handlers(void)
{
    static const struct wow_handlers16 handlers16 =
    {
        button_proc16,
        combo_proc16,
        listbox_proc16,
    };

    UserRegisterWowHandlers( &handlers16, &wow_handlers32 );
}
