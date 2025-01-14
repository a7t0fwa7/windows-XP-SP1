/*
 * copy.h - File copy handler module description.
 */


/* Prototypes
 *************/

/* copy.c */

extern void BeginCopy(void);
extern void EndCopy(void);
extern TWINRESULT CopyHandler(PRECNODE, RECSTATUSPROC, LPARAM, DWORD, HWND, HWND);
extern BOOL NotifyReconciliationStatus(RECSTATUSPROC, UINT, LPARAM, LPARAM);
extern BOOL CreateFolders(LPCTSTR, HPATH);
extern BOOL DestroySubtree(LPCTSTR, HPATH);

#ifdef DEBUG

extern BOOL IsValidRecStatusProcMsg(UINT);

#endif


