export module slotmap:free;

export import :free.bitmap;
export import :free.defer;

#ifdef SLOTMAP_ENABLE_FREELIST
export import :free.freelist;
#endif

