//***************************************************************************
//	Audio decoder header
//
//***************************************************************************

#ifndef __CADEC_H__
#define __CADEC_H__


UCHAR INVERSE_BYTE( PHW_DEVICE_EXTENSION pHwDevExt, UCHAR uc );


// ***************************************************************************
//        T C 6 8 0 0 A F
// ***************************************************************************

void AUDIO_TC6800_INIT_PCM(PHW_DEVICE_EXTENSION pHwDevExt);
void AUDIO_TC6800_INIT_AC3(PHW_DEVICE_EXTENSION pHwDevExt );
void AUDIO_TC6800_INIT_MPEG(PHW_DEVICE_EXTENSION pHwDevExt );
void AUDIO_TC6800_DATA_OFF(PHW_DEVICE_EXTENSION pHwDevExt );

// ***************************************************************************
//        Z R 3 8 5 2 1 
// ***************************************************************************

void AUDIO_ZR385_OUT( PHW_DEVICE_EXTENSION pHwDevExt, UCHAR val );
void AUDIO_ZR385_DOWNLOAD( PHW_DEVICE_EXTENSION pHwDevExt, PUCHAR pData, ULONG size );
void AUDIO_ZR38521_BOOT_AC3(PHW_DEVICE_EXTENSION pHwDevExt);
void AUDIO_ZR38521_BOOT_MPEG(PHW_DEVICE_EXTENSION pHwDevExt);
void AUDIO_ZR38521_BOOT_PCM(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_CFG(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_PCMX(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_AC3(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_MPEG1(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_PLAY(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_MUTE_OFF(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_MUTE_ON(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_STOP(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_STOPF(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_STCR(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_VDSCR_ON( PHW_DEVICE_EXTENSION pHwDevExt, ULONG stc );
NTSTATUS AUDIO_ZR38521_VDSCR_OFF( PHW_DEVICE_EXTENSION pHwDevExt, ULONG stc );
NTSTATUS AUDIO_ZR38521_AVSYNC_OFF( PHW_DEVICE_EXTENSION pHwDevExt, ULONG stc );
NTSTATUS AUDIO_ZR38521_AVSYNC_ON( PHW_DEVICE_EXTENSION pHwDevExt, ULONG stc );
NTSTATUS AUDIO_ZR38521_STAT( PHW_DEVICE_EXTENSION pHwDevExt, PULONG pDiff );
NTSTATUS AUDIO_ZR38521_KCOEF(PHW_DEVICE_EXTENSION pHwDevExt);
void AUDIO_ZR38521_REPEAT_02(PHW_DEVICE_EXTENSION pHwDevExt);
void AUDIO_ZR38521_REPEAT_16(PHW_DEVICE_EXTENSION pHwDevExt);
NTSTATUS AUDIO_ZR38521_BFST( PHW_DEVICE_EXTENSION pHwDevExt, PULONG pErrCode );

// ***************************************************************************
//        T C 9 4 2 5 F
// ***************************************************************************

void AUDIO_TC9425_INIT_DIGITAL(PHW_DEVICE_EXTENSION pHwDevExt);
void AUDIO_TC9425_INIT_ANALOG(PHW_DEVICE_EXTENSION pHwDevExt);
void AUDIO_TC9425_SET_VOLUME( PHW_DEVICE_EXTENSION pHwDevExt, ULONG vol );

#endif	// __CADEC_H__
