/*
 * resstr.c - Return code to string translation routines.
 *
 * Taken from URL code
 *
 * Created: ChrisPi 9-11-95
 *
 */


/* Headers
 **********/

#include "precomp.h"

/* Macros
 *********/

/*
 * macro for simplifying result to string translation, assumes result string
 * pointer pcsz
 */

#define STRING_CASE(val)               case val: pcsz = #val; break


/****************************** Public Functions *****************************/


#ifdef DEBUG

PCSTR GetINTString(int n)
{
   static char s_rgchINT[] = "-2147483646";

   wsprintfA(s_rgchINT, "%d", n);

   ASSERT(IS_VALID_STRING_PTR_A(s_rgchINT, CSTR));

   return(s_rgchINT);
}


PCSTR GetINT_PTRString(INT_PTR n)
{
   static char s_rgchINT[] = "-2147483646xxxxxxxxxx";

   wsprintfA(s_rgchINT, "%Id", n);

   ASSERT(IS_VALID_STRING_PTR_A(s_rgchINT, CSTR));

   return(s_rgchINT);
}


PCSTR GetULONGString(ULONG ul)
{
   static char s_rgchULONG[] = "4294967295";

   wsprintfA(s_rgchULONG, "%lx", ul);

   ASSERT(IS_VALID_STRING_PTR_A(s_rgchULONG, CSTR));

   return(s_rgchULONG);
}


PCSTR GetBOOLString(BOOL bResult)
{
   PCSTR pcsz;

   pcsz = bResult ? "TRUE" : "FALSE";

   ASSERT(IS_VALID_STRING_PTR_A(pcsz, CSTR));

   return(pcsz);
}


PCSTR GetPVOIDString(PVOID ptr)
{
   static char s_rgchPVOID[] = "0xffff0000ffff0000";

   wsprintfA(s_rgchPVOID, "0x%p", ptr);

   ASSERT(IS_VALID_STRING_PTR_A(s_rgchPVOID, CSTR));

   return(s_rgchPVOID);
}

PCSTR GetClipboardFormatNameString(UINT ucf)
{
   PCSTR pcsz;
   static char s_szCFName[MAX_PATH];

   switch (ucf)
   {
      STRING_CASE(CF_TEXT);
      STRING_CASE(CF_BITMAP);
      STRING_CASE(CF_METAFILEPICT);
      STRING_CASE(CF_SYLK);
      STRING_CASE(CF_DIF);
      STRING_CASE(CF_TIFF);
      STRING_CASE(CF_OEMTEXT);
      STRING_CASE(CF_DIB);
      STRING_CASE(CF_PALETTE);
      STRING_CASE(CF_PENDATA);
      STRING_CASE(CF_RIFF);
      STRING_CASE(CF_WAVE);
      STRING_CASE(CF_UNICODETEXT);
      STRING_CASE(CF_ENHMETAFILE);
      STRING_CASE(CF_HDROP);
      STRING_CASE(CF_LOCALE);
      STRING_CASE(CF_MAX);
      STRING_CASE(CF_OWNERDISPLAY);
      STRING_CASE(CF_DSPTEXT);
      STRING_CASE(CF_DSPBITMAP);
      STRING_CASE(CF_DSPMETAFILEPICT);
      STRING_CASE(CF_DSPENHMETAFILE);

      default:
         if (! GetClipboardFormatNameA(ucf, s_szCFName, sizeof(s_szCFName)))
            lstrcpyA(s_szCFName, "UNKNOWN CLIPBOARD FORMAT");
         pcsz = s_szCFName;
         break;
   }

   ASSERT(IS_VALID_STRING_PTR_A(pcsz, CSTR));

   return(pcsz);
}


PCSTR GetCOMPARISONRESULTString(COMPARISONRESULT cr)
{
   PCSTR pcsz;

   switch (cr)
   {
      STRING_CASE(CR_FIRST_SMALLER);
      STRING_CASE(CR_FIRST_LARGER);
      STRING_CASE(CR_EQUAL);

      default:
         ERROR_OUT(("GetCOMPARISONRESULTString() called on unknown COMPARISONRESULT %d.",
                    cr));
         pcsz = "UNKNOWN COMPARISONRESULT";
         break;
   }

   ASSERT(IS_VALID_STRING_PTR_A(pcsz, CSTR));

   return(pcsz);
}

PCSTR GetHRESULTString(HRESULT hr)
{
   PCSTR pcsz;
   static char s_rgchHRESULT[] = "0x12345678";

   switch (hr)
   {
      STRING_CASE(S_OK);
      STRING_CASE(S_FALSE);

      STRING_CASE(DRAGDROP_S_CANCEL);
      STRING_CASE(DRAGDROP_S_DROP);
      STRING_CASE(DRAGDROP_S_USEDEFAULTCURSORS);

      STRING_CASE(E_UNEXPECTED);
      STRING_CASE(E_NOTIMPL);
      STRING_CASE(E_OUTOFMEMORY);
      STRING_CASE(E_INVALIDARG);
      STRING_CASE(E_NOINTERFACE);
      STRING_CASE(E_POINTER);
      STRING_CASE(E_HANDLE);
      STRING_CASE(E_ABORT);
      STRING_CASE(E_FAIL);
      STRING_CASE(E_ACCESSDENIED);

      STRING_CASE(CLASS_E_NOAGGREGATION);

      STRING_CASE(CO_E_NOTINITIALIZED);
      STRING_CASE(CO_E_ALREADYINITIALIZED);
      STRING_CASE(CO_E_INIT_ONLY_SINGLE_THREADED);

      STRING_CASE(DV_E_DVASPECT);
      STRING_CASE(DV_E_LINDEX);
      STRING_CASE(DV_E_TYMED);
      STRING_CASE(DV_E_FORMATETC);

#ifdef __INTSHCUT_H__

      STRING_CASE(E_FLAGS);

      STRING_CASE(URL_E_INVALID_SYNTAX);
      STRING_CASE(URL_E_UNREGISTERED_PROTOCOL);

      STRING_CASE(IS_E_EXEC_FAILED);

      STRING_CASE(E_FILE_NOT_FOUND);
      STRING_CASE(E_PATH_NOT_FOUND);

#endif

		//  ULS error codes
		//
      default:
         wsprintfA(s_rgchHRESULT, "%#lx", hr);
         pcsz = s_rgchHRESULT;
         break;
   }

   ASSERT(IS_VALID_STRING_PTR_A(pcsz, CSTR));

   return(pcsz);
}

#endif   /* DEBUG */

