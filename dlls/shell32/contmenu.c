/*
 *	IContextMenu
 *
 *	Copyright 1998	Juergen Schmied <juergen.schmied@metronet.de>
 */
#include "windows.h"
#include "winerror.h"
#include "debug.h"
#include "shlobj.h"
#include "shell32_main.h"

#define IDM_EXPLORE  0
#define IDM_OPEN     1
#define IDM_RENAME   2
#define IDM_LAST     IDM_RENAME

#define __T(x)      x
#define _T(x)       __T(x)
#define TEXT        _T

static HRESULT WINAPI IContextMenu_QueryInterface(LPCONTEXTMENU ,REFIID , LPVOID *);
static ULONG WINAPI IContextMenu_AddRef(LPCONTEXTMENU);
static ULONG WINAPI IContextMenu_Release(LPCONTEXTMENU);
static HRESULT WINAPI IContextMenu_QueryContextMenu(LPCONTEXTMENU , HMENU32 ,UINT32 ,UINT32 ,UINT32 ,UINT32);
static HRESULT WINAPI IContextMenu_InvokeCommand(LPCONTEXTMENU, LPCMINVOKECOMMANDINFO);
static HRESULT WINAPI IContextMenu_GetCommandString(LPCONTEXTMENU , UINT32 ,UINT32 ,LPUINT32 ,LPSTR ,UINT32);

BOOL32 IContextMenu_AllocPidlTable(LPCONTEXTMENU, DWORD);
void IContextMenu_FreePidlTable(LPCONTEXTMENU);
BOOL32 IContextMenu_CanRenameItems(LPCONTEXTMENU);
BOOL32 IContextMenu_FillPidlTable(LPCONTEXTMENU, LPCITEMIDLIST *, UINT32);

static struct IContextMenu_VTable cmvt = 
{	IContextMenu_QueryInterface,
	IContextMenu_AddRef,
    IContextMenu_Release,
	IContextMenu_QueryContextMenu,
	IContextMenu_InvokeCommand,
	IContextMenu_GetCommandString
};
/**************************************************************************
*  IContextMenu_QueryInterface
*/
static HRESULT WINAPI IContextMenu_QueryInterface(LPCONTEXTMENU this,REFIID riid, LPVOID *ppvObj)
{ char    xriid[50];
  WINE_StringFromCLSID((LPCLSID)riid,xriid);
  TRACE(shell,"(%p)->(\n\tIID:\t%s,%p)\n",this,xriid,ppvObj);

  *ppvObj = NULL;

  if(IsEqualIID(riid, &IID_IUnknown))          /*IUnknown*/
  { *ppvObj = (LPUNKNOWN)(LPCONTEXTMENU)this; 
  }
  else if(IsEqualIID(riid, &IID_IContextMenu))  /*IContextMenu*/
  { *ppvObj = (LPCONTEXTMENU)this;
  }   
  else if(IsEqualIID(riid, &IID_IShellExtInit))  /*IShellExtInit*/
  { *ppvObj = (LPSHELLEXTINIT)this;
  }   

  if(*ppvObj)
  { (*(LPCONTEXTMENU*)ppvObj)->lpvtbl->fnAddRef(this);      
    TRACE(shell,"-- Interface: (%p)->(%p)\n",ppvObj,*ppvObj);
    return S_OK;
  }
  TRACE(shell,"-- Interface: E_NOINTERFACE\n");
  return E_NOINTERFACE;
}   

/**************************************************************************
*  IContextMenu_AddRef
*/
static ULONG WINAPI IContextMenu_AddRef(LPCONTEXTMENU this)
{ TRACE(shell,"(%p)->(count=%lu)\n",this,(this->ref)+1);
  return ++(this->ref);
}
/**************************************************************************
*  IContextMenu_Release
*/
static ULONG WINAPI IContextMenu_Release(LPCONTEXTMENU this)
{ TRACE(shell,"(%p)->()\n",this);
  if (!--(this->ref)) 
  { TRACE(shell," destroying IContextMenu(%p)\n",this);

	if(this->pSFParent)
	  this->pSFParent->lpvtbl->fnRelease(this->pSFParent);

	/*make sure the pidl is freed*/
	if(this->aPidls)
	{ IContextMenu_FreePidlTable(this);
	}

	if(this->pPidlMgr)
   	  PidlMgr_Destructor(this->pPidlMgr);

    HeapFree(GetProcessHeap(),0,this);
    return 0;
  }
  return this->ref;
}

/**************************************************************************
*   IContextMenu_Constructor()
*/
LPCONTEXTMENU IContextMenu_Constructor(LPSHELLFOLDER pSFParent, LPCITEMIDLIST *aPidls, UINT32 uItemCount)
{	LPCONTEXTMENU cm;
	UINT32  u;
    
	cm = (LPCONTEXTMENU)HeapAlloc(GetProcessHeap(),0,sizeof(IContextMenu));
	cm->lpvtbl=&cmvt;
	cm->ref = 1;

	cm->pSFParent = pSFParent;
	if(cm->pSFParent)
	   cm->pSFParent->lpvtbl->fnAddRef(cm->pSFParent);

	cm->aPidls = NULL;
	cm->pPidlMgr = PidlMgr_Constructor();

	IContextMenu_AllocPidlTable(cm, uItemCount);
    
	if(cm->aPidls)
	{ IContextMenu_FillPidlTable(cm, aPidls, uItemCount);
	}

	cm->bAllValues = 1;
	for(u = 0; u < uItemCount; u++)
    { cm->bAllValues &= (cm->pPidlMgr->lpvtbl->fnIsValue(cm->pPidlMgr, aPidls[u]) ? 1 : 0);
	}
    TRACE(shell,"(%p)->()\n",cm);
    return cm;
}


/**************************************************************************
* IContextMenu_QueryContextMenu()
*/

static HRESULT WINAPI  IContextMenu_QueryContextMenu( LPCONTEXTMENU this, HMENU32 hmenu,
							UINT32 indexMenu,UINT32 idCmdFirst,UINT32 idCmdLast,UINT32 uFlags)
{	BOOL32			fExplore ;
	MENUITEMINFO32A	mii;

	TRACE(shell,"(%p)->(hmenu=%x indexmenu=%x cmdfirst=%x cmdlast=%x flags=%x )\n",this, hmenu, indexMenu, idCmdFirst, idCmdLast, uFlags);
	if(!(CMF_DEFAULTONLY & uFlags))
	{ if(!this->bAllValues)
      { fExplore = uFlags & CMF_EXPLORE;
        if(fExplore)
        { ZeroMemory(&mii, sizeof(mii));
          mii.cbSize = sizeof(mii);
          mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
          mii.wID = idCmdFirst + IDM_EXPLORE;
          mii.fType = MFT_STRING;
          mii.dwTypeData = TEXT("&Explore");
          mii.fState = MFS_ENABLED | MFS_DEFAULT;
          InsertMenuItem32A( hmenu, indexMenu++, TRUE, &mii);

          ZeroMemory(&mii, sizeof(mii));
          mii.cbSize = sizeof(mii);
          mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
          mii.wID = idCmdFirst + IDM_OPEN;
          mii.fType = MFT_STRING;
          mii.dwTypeData = TEXT("&Open");
          mii.fState = MFS_ENABLED;
          InsertMenuItem32A( hmenu, indexMenu++, TRUE, &mii);
        }
        else
        { ZeroMemory(&mii, sizeof(mii));
          mii.cbSize = sizeof(mii);
          mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
          mii.wID = idCmdFirst + IDM_OPEN;
          mii.fType = MFT_STRING;
          mii.dwTypeData = TEXT("&Open");
          mii.fState = MFS_ENABLED | MFS_DEFAULT;
          InsertMenuItem32A( hmenu, indexMenu++, TRUE, &mii);

          ZeroMemory(&mii, sizeof(mii));
          mii.cbSize = sizeof(mii);
          mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
          mii.wID = idCmdFirst + IDM_EXPLORE;
          mii.fType = MFT_STRING;
          mii.dwTypeData = TEXT("&Explore");
          mii.fState = MFS_ENABLED;
          InsertMenuItem32A( hmenu, indexMenu++, TRUE, &mii);
        }

        if(uFlags & CMF_CANRENAME)
        { ZeroMemory(&mii, sizeof(mii));
          mii.cbSize = sizeof(mii);
          mii.fMask = MIIM_ID | MIIM_TYPE;
          mii.wID = 0;
          mii.fType = MFT_SEPARATOR;
          InsertMenuItem32A( hmenu, indexMenu++, TRUE, &mii);
 
          ZeroMemory(&mii, sizeof(mii));
          mii.cbSize = sizeof(mii);
          mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
          mii.wID = idCmdFirst + IDM_RENAME;
          mii.fType = MFT_STRING;
          mii.dwTypeData = TEXT("&Rename");
          mii.fState = (IContextMenu_CanRenameItems(this) ? MFS_ENABLED : MFS_DISABLED);
          InsertMenuItem32A( hmenu, indexMenu++, TRUE, &mii);
        }
      }
      return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (IDM_LAST + 1));
    }
	return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
}

/**************************************************************************
* IContextMenu_InvokeCommand()
*/
static HRESULT WINAPI IContextMenu_InvokeCommand(LPCONTEXTMENU this, LPCMINVOKECOMMANDINFO lpcmi)
{	LPITEMIDLIST      pidlTemp,pidlFQ;
	SHELLEXECUTEINFO  sei;
	int   i;

 	TRACE(shell,"(%p)->(execinfo=%p)\n",this,lpcmi);    

	if(HIWORD(lpcmi->lpVerb))
	{ //the command is being sent via a verb
	  return NOERROR;
	}

	if(LOWORD(lpcmi->lpVerb) > IDM_LAST)
	  return E_INVALIDARG;

	switch(LOWORD(lpcmi->lpVerb))
	{ case IDM_EXPLORE:
	  case IDM_OPEN:
        /* Find the first item in the list that is not a value. These commands 
      	should never be invoked if there isn't at least one key item in the list.*/

        for(i = 0; this->aPidls[i]; i++)
	    { if(!this->pPidlMgr->lpvtbl->fnIsValue(this->pPidlMgr, this->aPidls[i]))
            break;
        }
      
		pidlTemp = ILCombine(this->pSFParent->mpidl, this->aPidls[i]);
		pidlFQ = ILCombine(this->pSFParent->mpidlNSRoot, pidlTemp);
		SHFree(pidlTemp);
      
		ZeroMemory(&sei, sizeof(sei));
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_IDLIST | SEE_MASK_CLASSNAME;
		sei.lpIDList = pidlFQ;
		sei.lpClass = TEXT("folder");
		sei.hwnd = lpcmi->hwnd;
		sei.nShow = SW_SHOWNORMAL;
      
		if(LOWORD(lpcmi->lpVerb) == IDM_EXPLORE)
	    { sei.lpVerb = TEXT("explore");
        }
		else
        { sei.lpVerb = TEXT("open");
        }
        ShellExecuteEx32A(&sei);
		SHFree(pidlFQ);
        break;

	  case IDM_RENAME:
        MessageBeep32(MB_OK);
        /*handle rename for the view here*/
        break;
   	}
	return NOERROR;
}

/**************************************************************************
*  IContextMenu_GetCommandString()
*/
static HRESULT WINAPI IContextMenu_GetCommandString( LPCONTEXTMENU this, UINT32 idCommand,
						UINT32 uFlags,LPUINT32 lpReserved,LPSTR lpszName,UINT32 uMaxNameLen)
{	HRESULT  hr = E_INVALIDARG;

	TRACE(shell,"(%p)->(idcom=%x flags=%x %p name=%s len=%x)\n",this, idCommand, uFlags, lpReserved, lpszName, uMaxNameLen);

 	switch(uFlags)
	{ case GCS_HELPTEXT:
        hr = E_NOTIMPL;
        break;
   
	  case GCS_VERBA:
        switch(idCommand)
        { case IDM_RENAME:
            strcpy((LPSTR)lpszName, "rename");
            hr = NOERROR;
            break;
        }
        break;

   /* NT 4.0 with IE 3.0x or no IE will always call this with GCS_VERBW. In this 
   case, you need to do the lstrcpyW to the pointer passed.*/
	  case GCS_VERBW:
        switch(idCommand)
        { case IDM_RENAME:
            lstrcpyAtoW((LPWSTR)lpszName, "rename");
            hr = NOERROR;
            break;
        }
        break;

	  case GCS_VALIDATE:
        hr = NOERROR;
        break;
	}
	return hr;
}

/**************************************************************************
*  IContextMenu_AllocPidlTable()
*/
BOOL32 IContextMenu_AllocPidlTable(LPCONTEXTMENU this, DWORD dwEntries)
{	//add one for NULL terminator
	TRACE(shell,"(%p)->(entrys=%u)\n",this, dwEntries);
	dwEntries++;

	this->aPidls = (LPITEMIDLIST*)SHAlloc(dwEntries * sizeof(LPITEMIDLIST));

	if(this->aPidls)
	{ ZeroMemory(this->aPidls, dwEntries * sizeof(LPITEMIDLIST));	/*set all of the entries to NULL*/
	}
	return (this->aPidls != NULL);
}

/**************************************************************************
* IContextMenu_FreePidlTable()
*/
void IContextMenu_FreePidlTable(LPCONTEXTMENU this)
{   int   i;

	TRACE(shell,"(%p)->()\n",this);

	if(this->aPidls)
	{ for(i = 0; this->aPidls[i]; i++)
      { SHFree(this->aPidls[i]);
      }
   
	SHFree(this->aPidls);
	this->aPidls = NULL;
	}
}

/**************************************************************************
* IContextMenu_FillPidlTable()
*/
BOOL32 IContextMenu_FillPidlTable(LPCONTEXTMENU this, LPCITEMIDLIST *aPidls, UINT32 uItemCount)
{   UINT32  i;
	TRACE(shell,"(%p)->(apidl=%p count=%u)\n",this, aPidls, uItemCount);
	if(this->aPidls)
	{ for(i = 0; i < uItemCount; i++)
      { this->aPidls[i] = ILClone(aPidls[i]);
      }
      return TRUE;
 	}
	return FALSE;
}

/**************************************************************************
* IContextMenu_CanRenameItems()
*/
BOOL32 IContextMenu_CanRenameItems(LPCONTEXTMENU this)
{	UINT32  i;
	DWORD dwAttributes;

	TRACE(shell,"(%p)->()\n",this);

	if(this->aPidls)
	{ if(this->pPidlMgr)
	  { for(i = 0; this->aPidls[i]; i++){} /*get the number of items assigned to this object*/
        if(i > 1)	/*you can't rename more than one item at a time*/
        { return FALSE;
        }
	    dwAttributes = SFGAO_CANRENAME;
	    this->pSFParent->lpvtbl->fnGetAttributesOf(this->pSFParent, i,
        						 (LPCITEMIDLIST*)this->aPidls, &dwAttributes);
      
      return dwAttributes & SFGAO_CANRENAME;
      }
	}	
	return FALSE;
}

