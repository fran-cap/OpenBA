#include <cstdint>
#include "io/PackageFile.h"
#include "object/External.h"
namespace Platform { namespace Window { void ShowErrorMessage(const uint16_t*, const uint16_t*) {} } }
// P3 PoC link stubs: type-registry factories the PoC never calls.
Object::Base* IO::PackageFile::CreateNewObject() { return nullptr; }
Object::Base* IO::PackageFile::CreateInPlaceObject(void*) { return nullptr; }
Object::Base* Object::ExternalList::CreateNewObject() { return nullptr; }
Object::Base* Object::ExternalList::CreateInPlaceObject(void*) { return nullptr; }
#include "io/Stream.h"
// Write-path members the read-only PoCs never call.
