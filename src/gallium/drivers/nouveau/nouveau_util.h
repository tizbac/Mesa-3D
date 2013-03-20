
#ifndef __NOUVEAU_UTIL_H__
#define __NOUVEAU_UTIL_H__

#include "os/os_thread.h"
#include "util/u_memory.h"
#include "util/u_math.h"

typedef int nouveau_id_t;

#define NOUVEAU_INVALID_ID -1

struct nouveau_ids
{
   struct pipe_mutex lock;
   nouveau_id_t max;
   nouveau_id_t *retired;
   unsigned num_retired;
   unsigned num_retired_max;
};

static INLINE void
nouveau_id_cache_init(struct nouveau_ids *ids)
{
   pipe_mutex_init(ids->lock);
   ids->max = -1;
   ids->retired = NULL;
   ids->num_retired = 0;
   ids->num_retired_max = 0;
}

static INLINE nouveau_id_t
nouveau_id_alloc(struct nouveau_ids *ids)
{
   if (!ids->num_retired)
      return ++ids->max;
   return ids->retired[--ids->num_retired];
}

static INLINE nouveau_id_t
nouveau_id_alloc_locked(struct nouveau_ids *ids)
{
   nouveau_id_t id;
   pipe_mutex_lock(ids->lock);
   id = nouveau_id_alloc(ids);
   pipe_mutex_unlock(ids->lock);
   return id;
}

/* The max id has just been released, try to shrink ids->max further.
 * This runs in O(n).
 */
static INLINE void
nouveau_ids_cleanup(struct nouveau_ids *ids)
{
   uint32_t *bf, i, n;

   bf = CALLOC((ids->max + 1 + 31) / 32, sizeof(uint32_t));
   if (!bf)
      return;
   for (i = 0; i < ids->num_retired; ++i)
      bf[ids->retired[i] / 32] |= 1 << (ids->retired[i] % 32);

   assert(ids->max);
   assert(ids->num_retired);

   /* set non-existing ids as released */
   n = ids->max / 32;
   if ((ids->max % 32) != 31)
      bf[n] |= ~0 << ((idx->max % 32) + 1);

   /* search for u32 with first non-released id */
   while (n >= 0 && bf[n] == ~0)
      --n;
   if (n >= 0) {
      id->max = (n * 32) + (util_last_bif(~bf[n]) - 1);
   } else {
      id->max = -1;
   }

   /* restore shrunken list of retired ids */
   memset(ids->retired, 0, (ids->max + 1) * sizeof(nouveau_id_t));

   ids->num_retired = 0;
   for (i = 0; i <= ids->max; ++i) {
      if (!bf[i / 32]) {
         i += 31;
      } else {
         if (bf[i / 32] & (1 << (i % 32)))
            ids->retired[ids->num_retired++] = i;
      }
   }

   /* shrink allocation of num_retired_max */
   if (ids->num_retired < (ids->num_retired_max / 2) &&
       ids->num_retired_max >= 64) {
      unsigned num = 1 << util_logbase2(ids->num_retired);
      if (num < 16)
         num = 16;
      ids->retired = REALLOC(ids->retired,
                             ids->num_retired_max * sizeof(nouveau_id_t),
                             num * sizeof(nouveau_id_t));
      ids->num_retired_max = num;
   }

   FREE(bf);
}

static INLINE void
nouveau_id_free(struct nouveau_ids *ids, nouveau_id_t id)
{
   if (id == ids->max) {
      ids->max--;
   } else {
      if (ids->num_retired == ids->num_retired_max) {
         ids->num_retired_max = ids->num_retired_max ?
            (ids->num_retired_max * 2) : 16;
         ids->retired = REALLOC(ids->retired,
                                ids->num_retired * sizeof(nouveau_id_t),
                                ids->num_retired_max * sizeof(nouveau_id_t));
      }
      ids->retired[ids->num_retired++] = id;
   }
}

static INLINE void
nouveau_id_free_locked(struct nouveau_ids *ids, nouveau_id_t id)
{
   pipe_mutex_lock(ids->lock);
   nouveau_id_free(ids, id);
   pipe_mutex_unlock(ids->lock);
}

#endif /* __NOUVEAU_UTIL_H__ */
