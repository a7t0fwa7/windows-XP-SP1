// stdcdata.cpp : Implementation of CComponentData

#include "stdcdata.h"
#include "guidhelp.h" // ExtractObjectTypeGUID
#include "stddtobj.h" // CDataObject::m_CFRawCookie
#include "stdrsrc.h"  // IDS_FRAMEWORK_*

//
// CComponentData
//

CComponentData::CComponentData()
: m_pConsole( NULL ),
  m_pConsoleNameSpace( NULL )
{
}

CComponentData::~CComponentData()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
  ASSERT(NULL == m_pConsole && NULL == m_pConsoleNameSpace); // object should be Destroy()ed before being Release()d
  SAFE_RELEASE( m_pConsole );
  SAFE_RELEASE( m_pConsoleNameSpace );
}

HRESULT CComponentData::OnNotifyExpand(LPDATAOBJECT /*lpDataObject*/, BOOL /*bExpanding*/, HSCOPEITEM /*hParent*/)
{
  return S_OK;
}

HRESULT CComponentData::OnNotifyPreload(LPDATAOBJECT /*lpDataObject*/, HSCOPEITEM /*hRootScopeItem*/)
{
  return S_OK;
}

HRESULT CComponentData::OnNotifyRename(LPDATAOBJECT /*lpDataObject*/, LPARAM arg, LPARAM /*param*/)
{
  // by default, do not allow rename
  return (0 == arg) ? S_FALSE : S_OK;
}

HRESULT CComponentData::OnNotifyDelete(LPDATAOBJECT /*lpDataObject*/)
{
  ASSERT(FALSE); // Why did you enable the Delete command if you won't handle it?
  return S_OK;
}
HRESULT CComponentData::OnNotifyRelease(LPDATAOBJECT /*lpDataObject*/, HSCOPEITEM /*hItem*/)
{
  ASSERT(FALSE); // You will leak memory if you don't handle this
  return S_OK;
}

/////////////////////////////////////////////////////////////////////
// Virtual function called by CComponentData::IComponentData::Notify(MMCN_PROPERTY_CHANGE)
// OnPropertyChange() is generated by MMCPropertyChangeNotify( param )
HRESULT CComponentData::OnPropertyChange( LPARAM /*param*/ )
{
  return S_OK;
}


/*
 *  IComponentData
 */

STDMETHODIMP CComponentData::Initialize(LPUNKNOWN pUnknown)
{
	MFC_TRY;
    ASSERT(pUnknown != NULL);

    // MMC should only call ::Initialize once!
    ASSERT(NULL == m_pConsoleNameSpace);
  SAFE_RELEASE( m_pConsoleNameSpace );
    HRESULT hr = pUnknown->QueryInterface(IID_IConsoleNameSpace,
                    reinterpret_cast<void**>(&m_pConsoleNameSpace));
    ASSERT( SUCCEEDED(hr) );

    // add the images for the scope tree
    LPIMAGELIST lpScopeImage = NULL;

  ASSERT(NULL == m_pConsole);
  SAFE_RELEASE( m_pConsole );
    hr = pUnknown->QueryInterface(IID_IConsole, reinterpret_cast<void**>(&m_pConsole));
    ASSERT( SUCCEEDED(hr) );

    hr = m_pConsole->QueryScopeImageList(&lpScopeImage);
    ASSERT( SUCCEEDED(hr) );

    hr = LoadIcons(lpScopeImage, FALSE);
    ASSERT( SUCCEEDED(hr) );

    lpScopeImage->Release();

    MFC_CATCH;
    return S_OK;
}

STDMETHODIMP CComponentData::Notify(LPDATAOBJECT lpDataObject, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param)
{
    HRESULT hr = S_OK;
    MFC_TRY;

    switch(event)
    {
    case MMCN_EXPAND:
    hr = OnNotifyExpand( lpDataObject, (BOOL)arg, (HSCOPEITEM)param );
        break;
    case MMCN_RENAME:
    hr = OnNotifyRename( lpDataObject, arg, param );
        break;
    case MMCN_DELETE:
    hr = OnNotifyDelete( lpDataObject );
    break;
  case MMCN_REMOVE_CHILDREN:
    hr = OnNotifyRelease( lpDataObject, arg );
    break;

  case MMCN_PRELOAD:
    hr = OnNotifyPreload (lpDataObject, (HSCOPEITEM) arg);
    break;

  case MMCN_PROPERTY_CHANGE:
    // CODEWORK arg is "fScopePane", should this be passed on?
    hr = OnPropertyChange( param );
    break;

  default:
    TRACE1("INFO: CComponentData::Notify () - Unknown Event %d.\n", event); // add new method for this notification
    break;
    }

    MFC_CATCH;
    return hr;
}

STDMETHODIMP CComponentData::Destroy()
{
    MFC_TRY;
    SAFE_RELEASE(m_pConsoleNameSpace);
    SAFE_RELEASE(m_pConsole);
    MFC_CATCH;
  return S_OK;
}

STDMETHODIMP CComponentData::GetDisplayInfo(SCOPEDATAITEM* pScopeDataItem)
{
    MFC_TRY;
  // WARNING cookie cast
    CCookie* pcookie = reinterpret_cast<CCookie*>(pScopeDataItem->lParam);
    ASSERT(NULL != pcookie);

  ASSERT( NULL != pScopeDataItem ); // result items never have NULL cookie
  if (SDI_STR & pScopeDataItem->mask)
  {
    pScopeDataItem->displayname = QueryResultColumnText( *pcookie, 0 );
    if ( NULL == pScopeDataItem->displayname )
      pScopeDataItem->displayname = L""; // just in case
  }
  if ( (SDI_IMAGE|SDI_OPENIMAGE) & pScopeDataItem->mask )
  {
    pScopeDataItem->nImage = QueryImage(
      *pcookie, !!(SDI_OPENIMAGE & pScopeDataItem->mask) );
  }
  MFC_CATCH;
  return S_OK;
/*
    ASSERT(pScopeDataItem->mask == TVIF_TEXT);
  pScopeDataItem->displayname = QueryResultColumnText(*pcookie,0);
    ASSERT(pScopeDataItem->displayname != NULL);

    return S_OK;
*/
}

STDMETHODIMP CComponentData::CompareObjects(
  LPDATAOBJECT lpDataObjectA,
  LPDATAOBJECT lpDataObjectB)
{
  int nResult = COMPARESIMILARCOOKIE_FULL;  // a full cookie comparison is desired.
  MFC_TRY;
  GUID guidA, guidB;
  HRESULT hr = ::ExtractObjectTypeGUID( lpDataObjectA, &guidA );
  if ( FAILED(hr) )
    return hr;
  hr = ::ExtractObjectTypeGUID( lpDataObjectB, &guidB );
  if ( FAILED(hr) )
    return hr;
  if ( 0 != ::memcmp( &guidA, &guidB, sizeof(GUID) ) )
    return S_FALSE; // different nodetypes

  // If the two nodetypes are the same, both of these objects
  // must belong to this snapin

  // Extract cookies
  // WARNING cookie cast
  CCookie* pcookieA = NULL;
  hr = ExtractData( lpDataObjectA,
                      CDataObject::m_CFRawCookie,
            &pcookieA,
            sizeof(pcookieA) );
  if ( FAILED(hr) )
  {
    ASSERT( FALSE );
    return hr;
  }
  pcookieA = ActiveBaseCookie(pcookieA);
  // WARNING cookie cast
  CCookie* pcookieB = NULL;
  hr = ExtractData( lpDataObjectB,
                      CDataObject::m_CFRawCookie,
            &pcookieB,
            sizeof(pcookieB) );
  if ( FAILED(hr) )
  {
    ASSERT( FALSE );
    return hr;
  }
  pcookieB = ActiveBaseCookie(pcookieB);

  // Compare cookies
  if (pcookieA == pcookieB)
    return S_OK;
  hr = pcookieA->CompareSimilarCookies( pcookieB, &nResult );
  if( FAILED(hr) )
    return hr;

  MFC_CATCH;
  return (0 == nResult) ? S_OK : S_FALSE;
}

int
GetErrorMsg(
    IN DWORD    dwError, 
    OUT PTSTR*  pptzMsg
)
{
  ASSERT(dwError != ERROR_SUCCESS);

  int cch = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER 
        | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, dwError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (PTSTR)pptzMsg, 0, NULL);

  if (0 == cch)
  {
    static HMODULE g_hNetMod = 0;
    if (0 == g_hNetMod)
      g_hNetMod = GetModuleHandle (L"netmsg.dll");

    if (g_hNetMod)
      cch = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER
                        | FORMAT_MESSAGE_FROM_HMODULE,
                        g_hNetMod, dwError,
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                        (PTSTR)pptzMsg, 0, NULL);
  }

  return cch;
}

INT CComponentData::DoPopup( INT nResourceID, DWORD dwErrorNumber, LPCTSTR pszInsertionString, UINT fuStyle )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
  CString strTitle;
  VERIFY( strTitle.LoadString(
    (MB_ICONSTOP == (fuStyle & 0x000000F0))
    ? IDS_FRAMEWORK_TITLE_ERROR : IDS_FRAMEWORK_TITLE_WARNING ) );
  CString strError;
  if (0 != dwErrorNumber || NULL != pszInsertionString)
  {
    CString strFormat;
    VERIFY( strFormat.LoadString( nResourceID ) );

    strError.Format( strFormat, dwErrorNumber, pszInsertionString );

    if (0 != dwErrorNumber)
    {
      PTSTR pszErrorMsg = NULL;
      int cch = GetErrorMsg(dwErrorNumber, &pszErrorMsg);
      if (cch > 0)
      {
        strError += _T("\n\n");
        strError += pszErrorMsg;

        LocalFree(pszErrorMsg);
      }
    }
  }
  else
  {
    VERIFY( strError.LoadString( nResourceID ) );
  }

  INT iRetval = 0;
  USES_CONVERSION;
  HRESULT hr = m_pConsole->MessageBox(
    T2OLE(const_cast<LPTSTR>((LPCTSTR)strError)),
    T2OLE(const_cast<LPTSTR>((LPCTSTR)strTitle)),
    fuStyle,
    &iRetval );
  ASSERT( SUCCEEDED(hr) );

  return iRetval;
}

/////////////////////////////////////////////////////////////////////////////
// CMyComputerComponentData::ISnapinHelp members

// Help on IComponentData just returns the file and no particular topic
STDMETHODIMP CComponentData::GetHelpTopic(LPOLESTR* lpCompiledHelpFile)
{
  MFC_TRY;
  if (lpCompiledHelpFile == NULL)
      return E_INVALIDARG;

  CString szHelpFilePath;
  HRESULT hr = GetHtmlHelpFilePath( szHelpFilePath );
  if ( FAILED(hr) )
    return hr;

  *lpCompiledHelpFile = reinterpret_cast <LPOLESTR> (
      CoTaskMemAlloc ((szHelpFilePath.GetLength () + 1) * sizeof (wchar_t)));
  if ( NULL == *lpCompiledHelpFile )
    return E_OUTOFMEMORY;
  USES_CONVERSION;
  wcscpy (*lpCompiledHelpFile, T2OLE ((LPTSTR)(LPCTSTR) szHelpFilePath));
  
  MFC_CATCH;
  return S_OK;
}

STDMETHODIMP CComponentData::GetLinkedTopics(LPOLESTR* /*lpCompiledHelpFiles*/)
{
    return E_NOTIMPL;
}

HRESULT CComponentData::GetHtmlHelpFilePath( CString& strref ) const
{
  if ( GetHtmlHelpFileName().IsEmpty () )
    return E_NOTIMPL;

  UINT nLen = ::GetSystemWindowsDirectory (strref.GetBufferSetLength(2 * MAX_PATH), 2 * MAX_PATH);
  strref.ReleaseBuffer();
  if (0 == nLen)
  {
    ASSERT(FALSE);
    return E_FAIL;
  }

  strref += L"\\help\\";
  strref += GetHtmlHelpFileName();
  
  return S_OK;
}

/* no taskpads
STDMETHODIMP CComponentData::ExpandAndGet(
  HSCOPEITEM hsiStartFrom,
  LPDATAOBJECT pDataObject,
  HSCOPEITEM* phScopeItem )
{
  ASSERT(FALSE);
  return E_NOTIMPL;
}
*/