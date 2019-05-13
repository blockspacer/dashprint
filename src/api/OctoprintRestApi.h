#ifndef OCTOPRINTRESTAPI_H
#define OCTOPRINTRESTAPI_H
#include "web/web.h"
#include "FileManager.h"
#include "PrinterManager.h"

void routeOctoprintRest(std::shared_ptr<WebRouter> router, FileManager& fileManager, PrinterManager& printerManager);

#endif