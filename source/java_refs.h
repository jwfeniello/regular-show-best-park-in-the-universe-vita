#ifndef PARK_JAVA_REFS_H
#define PARK_JAVA_REFS_H
#include <stdbool.h>
#include <falso_jni/FalsoJNI.h>
enum ParkRefKind { PARK_REF_CLASS, PARK_REF_STRING, PARK_REF_ARRAY, PARK_REF_OBJECT };
void park_refs_install(void);
jobject park_ref_track(void *object,enum ParkRefKind kind,const char *class_name,void (*destroy)(void *));
const char *park_ref_class(jobject object);
void park_ref_keep(jobject object);
#endif
