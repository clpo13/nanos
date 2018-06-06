#include <runtime.h>
#include <kvm_platform.h>

extern void run64(u32 entry);

// there are a few of these little allocators
u64 working = 0x1000;

static u64 stage2_allocator(heap h, bytes b)
{
    u64 result = working;
    working += b;
    return working;
}

// xxx - instead of pulling the thread on generic storage support,
// we hardwire in the kernel resolution. revisit - it may be
// better to start populating the node tree here.
boolean lookup_kernel(void *fs, u64 *offset, u64 *length)
{
    u64 len;
    struct buffer b;
    b.contents = fs;
    b.start = 0;
    u64 off, loff, coff;
    if (!snode_lookup(&b, "children", &b.start, length)) return false;
    if (!snode_lookup(&b, "kernel", &b.start, length)) return false;
    if (!snode_lookup(&b, "contents", &coff, length)) return false;
    *offset = coff;
    return true;
}

// pass the memory parameters (end of load, end of mem)
void centry()
{
    struct heap workings;
    workings.alloc = stage2_allocator;
    heap working = &workings;

    console("stage2\n");
    // move this to the end of memory or the beginning of the pci gap
    // (under the begining of the kernel)
    u64 identity_start = 0x100000;
    u64 identity_length = 0x300000;

    for (region e = regions; region_type(e); e -= 1) {
         print_u64(region_type(e));
         console(" ");
         print_u64(region_base(e));
         console(" ");
         print_u64(region_length(e));                  
         console("\n");
         if (region_base(e) < identity_start) region_type(e) =REGION_FREE; 
         if (identity_start == region_base(e)) 
             region_base(e) = identity_start + identity_length;
    }

    create_region(identity_start, identity_length, REGION_IDENTITY);

    heap pages = region_allocator(working, PAGESIZE, REGION_IDENTITY);
    heap physical = region_allocator(working, PAGESIZE, REGION_PHYSICAL);
    void *vmbase = allocate_zero(pages, PAGESIZE);
    mov_to_cr("cr3", vmbase);
    map(identity_start, identity_start, identity_length, pages);
    // leak a page, and assume ph is in the first page
    void *header = allocate(physical, PAGESIZE);

    unsigned int fs_start = STAGE1SIZE + STAGE2SIZE;
    read_sectors(header, fs_start, PAGESIZE); // read in the head of the filesystem
    u64 kernel_length, kernel_offset;
    if (!lookup_kernel(header, &kernel_offset, &kernel_length)) {
        halt("unable to find kernel\n");
    }

    void *kernel = allocate(physical, pad(kernel_length, PAGESIZE));
    read_sectors(kernel, fs_start + kernel_offset, kernel_length);

    console("kernel: ");
    print_u64(*(u64 *)((char *)kernel+0x22b18));
    // should drop this in stage3? ... i think we just need
    // service32 and the stack.. this doesn't show up in the e820 regions
    // stack is currently in the first page, so lets leave it mapped
    // and take it out later...ideally move the stack here
    map(0, 0, 0xa000, pages);
    // tell stage3 that this is off limits..could actually move there
    create_region(0, 0xa0000, REGION_VIRTUAL);
    create_region(fs_start, 0, REGION_FILESYSTEM);
    // wrap
    struct buffer kb;
    kb.contents = kernel;
    kb.start = 0;
    kb.end = kernel_length;
    console("kernel length: ");
    print_u64(kernel_length);
    console("\n");
    void *k = load_elf(&kb, 0, pages, physical);
    run64(u64_from_pointer(k));
}
