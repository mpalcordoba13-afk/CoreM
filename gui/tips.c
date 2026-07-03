#include "tips.h"
#include "timer.h"

static const char *tips[] = {
    "Click derecho en el escritorio abre el menu rapido.",
    "Arrastra una ventana al borde para anclarla.",
    "Usa 'help' en la Terminal para ver todos los comandos.",
    "La calculadora tiene memoria: MC MR M+ M-.",
    "Cambia el tema en Configuracion (claro/oscuro).",
    "Tus archivos se guardan en RAM hasta que reinicies.",
    "Doble click en un icono del escritorio lo abre en Notas.",
    "El reloj usa el RTC real del hardware.",
};

const char* tips_get_today(void){
    int idx = (int)(timer_seconds()/37) % 8;
    return tips[idx];
}
