// Copyright (c) 2007 The Kaphan Foundation
//
// Possession of a copy of this file grants no permission or license
// to use, modify, or create derivate works.
//
// Please contact info@peerworks.org for further information.

#ifndef ITEM_H
#define ITEM_H 1

#include <config.h>
#if HAVE_JUDY_H
#include <Judy.h>
#endif

typedef struct ITEM {
  int id;
  int total_tokens;
  Pvoid_t tokens;
} Item, *PItem;

typedef struct TOKEN {
  int id;
  int frequency;
} Token, *Token_p;

extern Item * create_item             (int id);
extern Item * create_item_with_tokens (int id, int tokens[][2], int num_tokens);
extern int    item_get_id             (const Item *item);
extern int    item_get_total_tokens   (const Item *item);
extern int    item_get_num_tokens     (const Item *item);
extern int    item_get_token          (const Item *item, int token_id, Token_p token);
extern int    item_next_token         (const Item *item, Token_p token);
extern void   free_item               (Item *item);
/* This should only be called by item loaders */
extern int    item_add_token          (Item *item, int id, int frequency);

#endif