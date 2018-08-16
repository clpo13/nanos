#include <tfs_internal.h>
    
static CLOSURE_5_2(fs_read_extent, void,
                   filesystem, buffer, u64 *, merge, range, 
                   range, void *);
static void fs_read_extent(filesystem fs,
                           buffer target,
                           u64 *last,
                           merge m,
                           range q,
                           range ex,
                           void *val)
{
    range i = range_intersection(q, ex);
    // offset within a block - these are just the extents, so might be a sub
    status_handler f = apply(m);
    // last != start
    if (*last != 0) zero(buffer_ref(target, *last), target->start - *last);
    *last = q.end;
    target->end = *last;
    apply(fs->r, buffer_ref(target, i.start), range_span(i), u64_from_pointer(val), f);
}

// violate the g/s/i interface to allow offset and dest, I think there is an
// embedding but not today
void filesystem_read(tuple_handler t, void *dest, u64 length, u64 offset, status_handler completion)
{
    fsfile f = (fsfile)t;
    heap h = f->fs->h;
    u64 min, max;
    u64 *last = allocate_zero(f->fs->h, sizeof(u64));
    // b here is permanent - cache?
    buffer b = wrap_buffer(h, dest, length);
    merge m = allocate_merge(h, completion);
    range total = irange(offset, offset+length);
    rtrie_range_lookup(f->extents, total, closure(h, fs_read_extent, f->fs, b, last, m, total));
}

// extend here
static CLOSURE_4_2(fs_write_extent, void,
                   filesystem, buffer, merge, u64 *, 
                   range, void *);
static void fs_write_extent(filesystem fs, buffer source, merge m, u64 *last, range x, void *val)
{
    buffer segment = source; // not really
    // if this doesn't lie on an alignment bonudary we may need to do a read-modify-write
    status_handler sh = apply(m);
    apply(fs->w, segment, x.start, sh);
}


static void extent_set(fsfile f, symbol foff, value v)
{
    u64 length, foffset, boffset;
    parse_int(alloca_wrap(symbol_string(foff)), 10, &foffset);
    parse_int(alloca_wrap(table_find(v, sym(length))), 10, &length);
    parse_int(alloca_wrap(table_find(v, sym(offset))), 10, &boffset);
    rtrie_insert(f->extents, foffset, length, pointer_from_u64(boffset));
    rtrie_remove(f->fs->free, boffset, length);
}

// also children get (that means get is create..damn)
static void children_set(fsfile f, symbol foff, value v)
{

}

static u64 extend(fsfile f, u64 foffset, u64 length)
{
    u64 plen = pad(length, f->fs->alignment);
    u64 storage = allocate_u64(f->fs->storage, plen);
    if (storage == u64_from_pointer(INVALID_ADDRESS)) {
        halt("out of storage");
    }
    set(f, intern_u64(foffset), timm("length", "%d", length,
                                     "storage", "%d", storage),
        ignore_status);
    return storage;
}

                   
// consider not overwriting the old version and fixing up the metadata
// on success update the length field
void filesystem_write(tuple_handler t, buffer b, u64 offset, status_handler completion)
{
    fsfile f = (fsfile)t;
    u64 len = buffer_length(b);
    heap h = f->fs->h;     // leak
    u64 *last = allocate(h, sizeof(u64));
    *last = offset;

    merge m = allocate_merge(h, completion);
    rtrie_range_lookup(f->extents, irange(offset, offset+len), closure(h, fs_write_extent, f->fs, b, m, last));
    
    if (*last < (offset + len)) {
        u64 elen = (offset + len) - *last;
        u64 eoff = extend(f, *last, len);
        if (eoff != u64_from_pointer(INVALID_ADDRESS)) {
            status_handler sh = apply(m);
            apply(f->fs->w, wrap_buffer(transient, buffer_ref(b, *last), b->end - *last), eoff, sh);
        }
    }
}

// should be passing status to the client
static CLOSURE_2_1(read_entire_complete, void, value_handler, buffer, status);
static void read_entire_complete(value_handler bh, buffer b, status s)
{
    apply(bh, b);
}

static void directory_set(tuple_handler t, symbol s, value v, status_handler complete)
{
        fsfile f = (fsfile)t;
}

static void file_set(tuple_handler t, symbol s, value v, status_handler complete)
{
    fsfile f = (fsfile)t;
    if (s == sym(extents)) {
    }
    if (s == sym(children)) {
    }
    
}

// doesn't get called by backed
// trajectory transient?
static void file_get(tuple t, symbol n, value_handler success)
{
    fsfile f = (fsfile)t;

    u64 len = pad(file_length(t), 512);
    // block aligned
    buffer b = allocate_buffer(f->fs->fh, len);
    u64 *last = allocate_zero(f->fs->h, sizeof(u64));
    merge m = allocate_merge(f->fs->h,  closure(f->fs->h, read_entire_complete, success, b));
    status_handler k = apply(m); // hold a reference until we're sure we've issued everything
    rtrie_range_lookup(f->extents, irange(0, len),
                       closure(f->fs->h, fs_read_extent, f->fs, b, last, m, irange(0, len)));
}

static CLOSURE_0_2(decode_alloc, value, value, symbol);
static value decode_alloc(value a, symbol n)
{
}

void flush(value t, status_handler s)
{
    fsfile f = (fsfile)t;
    log_flush(f->fs->tl);
}

void create_filesystem(heap h,
                       u64 alignment,
                       u64 size,
                       block_read read,
                       block_write write,
                       value_handler complete)
{
    filesystem fs = allocate(h, sizeof(struct filesystem));
    fs->r = read;
    fs->h = h;
    fs->w = write;
    //    fs->root = // allocate backed
    fs->alignment = alignment;
    fs->free = rtrie_create(h);
    rtrie_insert(fs->free, 0, size, (void *)true); 
    rtrie_remove(fs->free, 0, INITIAL_LOG_SIZE);
    fs->storage = rtrie_allocator(h, fs->free);
    fs->tl = log_create(h, fs, closure(h, decode_alloc), complete);
}

