  TARGET = ocgcore
  TEMPLATE = lib
  INCLUDEPATH += .
  include(../../Kygo_qt.pri)
  CONFIG += staticlib
  

  INCLUDEPATH += ../lua

  HEADERS += card.h \
           common.h \
           duel.h \
           effect.h \
           effectset.h \
           field.h \
           group.h \
           interpreter.h \
           mtrandom.h \
           ocgapi.h \
           scriptlib.h
SOURCES += duel.cpp \
           effect.cpp \
           field.cpp \
           group.cpp \
           interpreter.cpp \
           libcard.cpp \
           libdebug.cpp \
           libduel.cpp \
           libeffect.cpp \
           libgroup.cpp \
           mem.cpp \
           ocgapi.cpp \
           operations.cpp \
           playerop.cpp \
           processor.cpp \
           scriptlib.cpp
