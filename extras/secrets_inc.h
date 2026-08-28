#ifndef SECRETS_INC_H
#define SECRETS_INC_H

#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  else
#    include "secrets.example.h"
#  endif
#else
#  include "secrets.example.h"
#endif

#endif
