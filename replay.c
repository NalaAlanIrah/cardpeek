/********************************************************************** 
*
* This file is part of Cardpeek, the smart card reader utility.
*
* Copyright 2009-2013 by Alain Pannetrat <L1L1@gmx.com>
*
* Cardpeek is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Cardpeek is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Cardpeek.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include "replay.h"
#include <stdlib.h>
#include <stdio.h>
#include "misc.h"
#include <glib/gstdio.h>

static anyreplay_t* cardreplay_new_item(cardreplay_t* ce, int type)
{
  replay_t replay;

  if (type==CARDREPLAY_COMMAND)
  {
    replay.com = (comreplay_t *)malloc(sizeof(comreplay_t));
    replay.com -> type = CARDREPLAY_COMMAND;
  }
  else
  {
    replay.res = (resreplay_t *)malloc(sizeof(resreplay_t));
    replay.res -> type = CARDREPLAY_RESET;
  }

  if (ce->start.any==NULL)
  {
    replay.any -> next = NULL;
    ce->start = ce->pos = replay;
  }
  else
  {
    replay.any -> next = (ce->pos.any)->next;
    (ce->pos.any)->next= replay.any;
    ce->pos = replay;
  }
  return replay.any;
}

int cardreplay_add_command(cardreplay_t* ce, const bytestring_t* command, unsigned sw, const bytestring_t* result)
{
  comreplay_t* com = (comreplay_t *)cardreplay_new_item(ce,CARDREPLAY_COMMAND);
 
  if (com==NULL)
   return CARDREPLAY_ERROR;
 
  com->query    = bytestring_duplicate(command);
  com->sw       = sw;
  com->response = bytestring_duplicate(result);
  ce->count++;
  return CARDREPLAY_OK;
}

int cardreplay_add_reset(cardreplay_t* ce, const bytestring_t* atr)
{
  resreplay_t* res = (resreplay_t *)cardreplay_new_item(ce,CARDREPLAY_RESET);

  if (res==NULL)
    return CARDREPLAY_ERROR;
  
  res->atr = bytestring_duplicate(atr);
  ce->count++;
  return CARDREPLAY_OK;
} 

cardreplay_t* cardreplay_new(void)
{
  cardreplay_t* ce=(cardreplay_t*)malloc(sizeof(cardreplay_t));
  ce -> start.any = ce -> pos.any = ce -> atr.any = NULL;
  ce -> count = 0;
  return ce;
}

void cardreplay_free(cardreplay_t* ce)
{
  replay_t item;
  anyreplay_t *next;

  if (ce==NULL)
  {
    log_printf(LOG_WARNING,"cardreplay_free(): Attempt to free an NULL pointer.");
    return;
  }

  item = ce -> start;
  
  while (item.any)
  {
    next = item.any -> next;
    if (item.any->type==CARDREPLAY_COMMAND)
    {
      bytestring_free(item.com->query);
      bytestring_free(item.com->response);
    }
    else
    {
      bytestring_free(item.res->atr);
    }
    free(item.any);
    item.any = next;
  }
  free(ce);
}

anyreplay_t* cardreplay_after_atr(cardreplay_t* ce)
{
  if (ce->atr.any==NULL)
    return NULL;
  return (ce->atr.any)->next;
}

int cardreplay_run_command(cardreplay_t* ce, 
			 const bytestring_t* command, 
			 unsigned short *sw, 
			 bytestring_t *response)
{
  anyreplay_t* init;
  replay_t cur; 

  bytestring_clear(response);
  *sw=0x6FFF;

  if (ce->start.any==NULL || ce->pos.any==NULL)
    return CARDREPLAY_ERROR;

  init    = ce->pos.any;
  cur.any = init;


  do
  {
    if (cur.any->type==CARDREPLAY_COMMAND)
    {
      if (bytestring_is_equal(command,cur.com->query))
      {
	bytestring_copy(response,cur.com->response);
	*sw = cur.com->sw;
	if (cur.any->next)
	  ce->pos.any = cur.any->next;
	else
	  ce->pos.any = cardreplay_after_atr(ce);
	return CARDREPLAY_OK;
      }
      if (cur.any->next)
	cur.any = cur.any->next;
      else
	cur.any = cardreplay_after_atr(ce);
    }
    else /* CARDREPLAY_RESET */
    {
      cur.any = cardreplay_after_atr(ce);
    }

  } while (cur.any!=init && cur.any!=NULL);

  return CARDREPLAY_ERROR;
}

int cardreplay_run_cold_reset(cardreplay_t* ce)
{
  if (ce->start.any==NULL)
    return CARDREPLAY_ERROR;

  ce->atr=ce->start; 
  ce->pos.any=(ce->start.any)->next;

  if ((ce->atr.res)->type!=CARDREPLAY_RESET)
  {
    log_printf(LOG_ERROR,"cardreplay_run_cold_atr(): reset error.");
    return CARDREPLAY_ERROR;
  }
  return CARDREPLAY_OK;
}

int cardreplay_run_warm_reset(cardreplay_t* ce)
{
  replay_t cur = ce->pos;

  if (ce->start.any==NULL || cur.any==NULL)
    return CARDREPLAY_ERROR;

  if (ce->atr.any==NULL)
    log_printf(LOG_WARNING,"cardreplay_run_warm_reset(): no previous cold reset");

  for (;;)
  {
    if (cur.any->type==CARDREPLAY_RESET)
    {
      ce->atr=cur;
      ce->pos.any=cur.any->next;
      return CARDREPLAY_OK;
    }
    if (cur.any->next)
      cur.any=cur.any->next;
    else if (ce->atr.any)
      cur = ce->atr;
    else
      cur = ce->start;
  }
  return CARDREPLAY_OK;
}

int cardreplay_run_last_atr(const cardreplay_t* ce, bytestring_t *atr)
{
  if (ce->atr.any==NULL)
  {
    bytestring_clear(atr);
    return CARDREPLAY_ERROR;
  }
  bytestring_copy(atr,(ce->atr.res)->atr);
  return CARDREPLAY_OK;
}

int cardreplay_save_to_file(const cardreplay_t* ce, const char *filename)
{
  FILE *f;
  replay_t cur;
  char *a;
  char *b;

  if ((f=g_fopen(filename,"w"))==NULL)
    return CARDREPLAY_ERROR;
  fprintf(f,"# cardpeek trace file\n");
  fprintf(f,"# version 0\n");

  for (cur=ce->start;cur.any!=NULL;cur.any=cur.any->next)
  {
    if (cur.any->type==CARDREPLAY_COMMAND)
    {
      a = bytestring_to_format("%D",cur.com->query);
      b = bytestring_to_format("%D",cur.com->response);
      fprintf(f,"C:%s:%04X:%s\n",a,cur.com->sw,b);
      free(a);
      free(b);
    }
    else /* CARDTREE_RESET */
    {
      a = bytestring_to_format("%D",cur.res->atr);
      fprintf(f,"R:%s\n",a);
      free(a);
    }
  }
  fprintf(f,"# end\n");
  fclose(f);
  return CARDREPLAY_OK;
}

cardreplay_t* cardreplay_new_from_file(const char *filename)
{
  FILE* f;
  cardreplay_t *ce;
  char BUF[1024];
  char *a;
  char *b;
  char *c;
  char *p;
  bytestring_t* query;
  bytestring_t* response;
  bytestring_t* atr;
  unsigned sw;
  int something_to_read;
  int lineno=0;

  if ((f=g_fopen(filename,"r"))==NULL)
    return NULL;

  ce = cardreplay_new();

  query = bytestring_new(8);
  response = bytestring_new(8);
  atr = bytestring_new(8);
  
  while ((something_to_read=(fgets(BUF,1024,f)!=NULL)))
  {
    lineno++;

    if (BUF[0]=='#')
      continue;
    if (BUF[0]=='C' && BUF[1]==':')
    {
      p=a=BUF+2;
      while (*p!=':' && *p) p++;
      if (*p==0)
	break;
      *p=0;
      p=b=p+1;
      while (*p!=':' && *p) p++;
      if (*p==0)
	break;
      *p=0;
      c=p+1;
      bytestring_assign_digit_string(query,a);
      sw = strtol(b,NULL,16);
      bytestring_assign_digit_string(response,c);
      cardreplay_add_command(ce,query,sw,response);
    }
    else if (BUF[0]=='R' && BUF[1]==':')
    {
      a=BUF+2;
      bytestring_assign_digit_string(atr,a);
      cardreplay_add_reset(ce,atr);
    }
    else if (BUF[0]=='\r' || BUF[0]=='\n')
      continue;
    else
      break;
  }
  bytestring_free(query);
  bytestring_free(response);
  bytestring_free(atr); 

  fclose(f);
  if (something_to_read)
  {
    log_printf(LOG_ERROR,
	 "cardreplay_new_from_file(): syntax error on line %i in %s",
	 lineno,filename);
    cardreplay_free(ce);
    return NULL;
  }
  return ce;
}

int cardreplay_count_records(const cardreplay_t* ce)
{
  return ce->count;
}

