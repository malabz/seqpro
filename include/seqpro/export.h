#ifndef SEQPRO_INCLUDE_SEQPRO_EXPORT_H_
#define SEQPRO_INCLUDE_SEQPRO_EXPORT_H_

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(SEQPRO_BUILDING_LIBRARY)
#define SEQPRO_EXPORT __declspec(dllexport)
#else
#define SEQPRO_EXPORT __declspec(dllimport)
#endif
#define SEQPRO_NO_EXPORT
#elif defined(__GNUC__) && __GNUC__ >= 4
#define SEQPRO_EXPORT __attribute__((visibility("default")))
#define SEQPRO_NO_EXPORT __attribute__((visibility("hidden")))
#else
#define SEQPRO_EXPORT
#define SEQPRO_NO_EXPORT
#endif

#endif  // SEQPRO_INCLUDE_SEQPRO_EXPORT_H_
