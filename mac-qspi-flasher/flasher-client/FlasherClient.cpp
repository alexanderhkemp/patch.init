//------------------------------------------------------------------------
// Copyright(c) 2024 Dad Design.
// 
// Utilitaire pour flasher la mémoire QSPIFlash à partir d'un PC
// Partie cliente
//-----------------------------------------------------------------------
#include "daisy_seed.h"
#include "Flasher.h"
#include "Buff.h"
//#pragma GCC optimize ("O0")

using namespace daisy;
#define VAL_TIMEOUT 100

// Variables globales 
DaisySeed hw;
Dad::stQSPI DSY_QSPI_BSS 	__QSPI;
Dad::cBuff 					___DataBuff(sizeof(Dad::Bloc)*2);
uint8_t	DSY_SDRAM_BSS 		__PageBuff[NB_BLOC_TRANS][TAILLE_BLOC_TRANS];

// Reception des données lues sur la liaison serie USB (COMxx)
void UsbCallback(uint8_t* buf, uint32_t* len)
{
	uint8_t* pBuff = buf;
	for(uint32_t Index=*len; Index != 0; Index--){
		___DataBuff.addData(*pBuff++);
	}
}

// Traitement d'un bloc
bool BlocProcess(uint16_t NumBloc){
Dad::Bloc* pBloc = (Dad::Bloc*)___DataBuff.getBuffPtr();
	
	// Test validité du bloc
	if((pBloc->StartMarker[0] != 'B') ||
	   (pBloc->StartMarker[1] != 'L') ||
	   (pBloc->StartMarker[2] != 'O') ||
	   (pBloc->StartMarker[3] != 'C') ||
	   (pBloc->EndMarker[0] != 'E') ||
	   (pBloc->EndMarker[1] != 'N') ||
	   (pBloc->EndMarker[2] != 'D') ||
	   (NumBloc != pBloc->NumBloc)
	  )
	{
		return false;
	}

	// Copie du bloc dans la mémoire de page et calcul du CRC
	uint8_t* pPageBuff = __PageBuff[NumBloc%NB_BLOC_TRANS];
	uint8_t* pBlocData = pBloc->Data;
	uint8_t	 CalcCRC = 0;
	for(uint16_t Index = 0; Index < TAILLE_BLOC_TRANS; Index++){
		CalcCRC += *pBlocData;
		*pPageBuff++ = *pBlocData++;
	}

	if(CalcCRC != pBloc->_CRC){
		return false;
	}
	
	// test fin de page -> Flash la page
	if(((NumBloc%NB_BLOC_TRANS)==(NB_BLOC_TRANS-1)) || (pBloc->_EndTrans != 0)){
		uint16_t NumPage = NumBloc/NB_BLOC_TRANS;
		uint32_t adresseData = (uint32_t) &__QSPI.Data[NumPage];
		hw.qspi.Erase(adresseData, adresseData + TAILLE_PAGES_QSPI);
		System::Delay(10);
    	hw.qspi.Write(adresseData, TAILLE_PAGES_QSPI, (uint8_t*)__PageBuff);
		System::Delay(10);

		// Vérification
		uint8_t * pQSPI = (uint8_t*) __QSPI.Data[NumPage];
		uint8_t * pPageBuff = (uint8_t*) __PageBuff;
		for (uint16_t Index = 0 ; Index < TAILLE_PAGES_QSPI; Index++){
			if(*pPageBuff++ != *pQSPI++){
				return false;
			}
		}
	}
	if(pBloc->_EndTrans != 0){
		return false;
	}else{
		return true;
	}
}

int main(void)
{
	hw.Init();
	hw.usb_handle.Init(UsbHandle::FS_INTERNAL);
    System::Delay(500);
    hw.usb_handle.SetReceiveCallback(UsbCallback, UsbHandle::FS_INTERNAL);
	
	uint16_t  		NumBloc = 0;
	Dad::MsgClient 	Msg;
	bool 			ResultProcess=false;
	
	Msg.StartMarker[0] = 'B';Msg.StartMarker[1] = 'L';Msg.StartMarker[2] = 'O';Msg.StartMarker[3] = 'C';
	
	while(1) {
		
		// Si erreur on retourne au premier bloc sinon bloc suivant
	    if(ResultProcess == false){
			NumBloc = 0;
			hw.SetLed(true);
		}else{
			NumBloc++;
		}
		
		// Emission d'un message vers le serveur
		___DataBuff.Clear();
		Msg.NumBloc = NumBloc;
        hw.usb_handle.TransmitInternal((uint8_t *)&Msg, sizeof(Msg));

		// Attente transmission d'un bloc
		uint16_t TimeOut = 0;
		while(___DataBuff.getNbData()<sizeof(Dad::Bloc) && (TimeOut<VAL_TIMEOUT)){
			TimeOut++;
			System::Delay(10);
		}
		
		// Traitement du bloc
		if(TimeOut<VAL_TIMEOUT){
			hw.SetLed(false);
			ResultProcess = BlocProcess(NumBloc);	
		}else{
			ResultProcess = false;
		}
	}
}
