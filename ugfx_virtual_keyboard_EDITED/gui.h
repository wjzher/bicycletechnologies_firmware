/******************************************************************************/
/* This file has been generated by the uGFX-Studio                            */
/*                                                                            */
/* http://ugfx.org                                                            */
/******************************************************************************/

#ifndef _GUI_H_
#define _GUI_H_

#include "gfx.h"

// GListeners
extern GListener glistener;

#define MAXIMUM_FRONT_GEARS 4
#define MAXIMUM_BACK_GEARS 9
#define MAXIMUM_TEETH 35
#define MINIMUM_TEETH 25

extern int gearFrontSettings[MAXIMUM_FRONT_GEARS+1];
extern int gearBackSettings[MAXIMUM_BACK_GEARS+1];

extern int currentGearSide;
extern int currentGearTeethWindow;
extern int currentTeethTeethWindow;
extern int oldMenuSelectedItem;

extern GFILE *myfile;

extern osMutexId mutex_id;

// Function Prototypes
void guiCreate(void);
void guiShowPage(unsigned pageIndex);
void guiEventLoop(void);

void printingFile(const char *fmt, ...);

#endif /* _GUI_H_ */

