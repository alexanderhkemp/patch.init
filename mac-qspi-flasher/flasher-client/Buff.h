//------------------------------------------------------------------------
// Copyright(c) 2024 Dad Design.
// 
// Classe de gestion d'un buffer mémoire
//-----------------------------------------------------------------------
#pragma once
#include "stdint.h"

namespace Dad {
    class cBuff{
    public:
        cBuff(uint16_t TailleFIFO){
            m_pStartBuff = new uint8_t[TailleFIFO];
            m_pEndBuff = &m_pStartBuff[TailleFIFO];
            Clear();
        };

        ~cBuff(){
            delete [] m_pStartBuff;m_pStartBuff = nullptr;
            m_pEndBuff = nullptr;
            Clear();
        };

        inline bool addData(uint8_t Data){
            if(m_pNextData  < m_pEndBuff){
                *m_pNextData++ =  Data;
                m_NbData++;
                return true;
            }else{
                return false;
            }
        }
        inline void Clear(){
            m_pNextData = m_pStartBuff;
            m_NbData = 0;
        }
        
        inline uint8_t *getBuffPtr(){
            return m_pStartBuff;
        }
        
        inline uint16_t getNbData(){
            return m_NbData;
        }

    
    protected :
        uint8_t * m_pStartBuff=nullptr; // Pointe sur l'adresse mémoire du buffer
        uint8_t * m_pEndBuff=nullptr;   // Pointe sur la dernière adresse mémoire du buffer
        uint8_t * m_pNextData=nullptr;  // Pointe sur la prochaine entree du buffer
        uint16_t m_NbData;              // Nombre de données dans le buffer
    };
}