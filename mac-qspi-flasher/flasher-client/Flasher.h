//------------------------------------------------------------------------
// Copyright(c) 2024 Dad Design.
// 
// Utilitaire pour flasher la mémoire QSPIFlash à partir d'un PC
// Structure de la mémoire et des blocs de transmission
//-----------------------------------------------------------------------
#pragma once
namespace Dad {

#define TAILLE_QSPI         8388608                         // Taille de la memoire flash QSPI 8M = 8 * 1024 * 1024
#define TAILLE_PAGES_QSPI   4096                            // 4K par page
#define NB_PAGES_QSPI       2048                            // Nombre de pages QSPI pour le flasher 1024 * 4096 4Mo

#define NB_BLOC_TRANS       4                               // Nombre de Blocs à transmettre par page QSPI  (1,2,4,8,16,32)
#define TAILLE_BLOC_TRANS   1024                            // Taille d'un bloc de transmission

#define MAX_NOM_ENTREE      40                              // Nombre de pages réservés en début de Flash QSPI
#define NB_DIR_FILES        20                              // Nombre d'entree dans le Directory

// Structure du directory
typedef struct File{
    char     Name[MAX_NOM_ENTREE];
    uint16_t Size;
    void *   pData;
} Directory[NB_DIR_FILES];

// Struture de la zone mémoire en QSPI utilisée par le flasher
typedef uint8_t Page[TAILLE_PAGES_QSPI];

struct stQSPI{
        Page Data[NB_PAGES_QSPI];   // Pages uilisées par les données des fichiers     
};

static_assert(sizeof(stQSPI) <= TAILLE_QSPI,"Mémoire utilisée > Taille mémoire Flash QSPI");

// Structure d'un bloc de transmission coté serveur
struct Bloc {
    char        StartMarker[4];             // Délimiteur debut de bloc "BLOC"
    uint16_t    NumBloc;                    // Numéro du bloc
    uint8_t     _CRC;                       // Checksum
    uint8_t     _EndTrans;                  // Identificateur de fin de transmission
    uint8_t     Data[TAILLE_BLOC_TRANS];    // Données
    char        EndMarker[3];               // Délimiteur fin de bloc "END"
};

// Structure d'un bloc de transmission coté client
struct MsgClient{
    char        StartMarker[4];             // Délimiteur debut de bloc "BLOC" , "STOP"
    uint16_t    NumBloc;                    // Numéro du bloc attendu
};

} //Dad