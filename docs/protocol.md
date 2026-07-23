# The tunix boot protocol

`src/protocol/protocol.h` is the ABI. A kernel includes that header; this
describes what it means and why it is shaped the way it is.

## The kernel asks

The usual arrangement is that the loader fills in a structure and hands over a
pointer to it. Every field added after the fact is then an ABI break, and every
kernel has to know which loader it is talking to.

Here it is the other way round. The kernel puts request structures **in its own
image**:

    struct boot_request {
        uint64_t magic[2];      /* BOOT_REQUEST_MAGIC_LOW, _HIGH */
        uint64_t id;
        uint64_t revision;      /* what the kernel understands */
        void *response;         /* null until the loader fills it in */
    };

The loader scans the loaded image for the magic pair on eight-byte boundaries
and answers each request it recognises.

Three consequences, and they are the point:

- An older kernel ignores anything added since it was written.
- A newer kernel asking for something this loader has never heard of finds
  `response` still null. That is not an error; it is the protocol saying so, and
  what to do about it is the kernel's business.
- Adding a request breaks nothing.

The magic is checked as a **pair**. One half will turn up in real data
eventually; both halves adjacent will not.

## Revisions

A request carries the revision the kernel understands. The loader answers at the
lower of that and its own, and puts which in the response. A kernel asking for
more than the loader knows gets what there is, and is told.

## The requests

| id | request | response |
|----|---------|----------|
| 1 | memory map | sorted, disjoint regions with a kind each |
| 2 | kernel address | physical and virtual base |
| 3 | command line | NUL terminated, never null |
| 4 | loader info | name and version |
| 5 | framebuffer | base, size, pitch, and the three channels |
| 6 | acpi | the address of the RSDP |
| 7 | processors | APIC ids, and which may be started |

### Memory map

Already folded: sorted, disjoint, and conservative where the firmware
contradicted itself. Where two entries claimed the same bytes, the more
restrictive kind won — memory claimed by nobody is worth less than memory
wrongly handed out.

It describes the machine **as it is now**, not as the firmware found it: the
loader's own arena is reported as reclaimable and the kernel's image as kernel
memory, both carved out of what would otherwise read as usable.

### Framebuffer

Channels are a shift and a width rather than a named layout, so a kernel packs a
colour the same way whichever firmware described the mode. `pitch` is carried
separately from `width` and is the one to use: firmware pads rows, and a kernel
computing the stride from the width draws a sheared picture.

A machine with no display leaves the response null. It is not given an empty
framebuffer, because a kernel handed a base address of zero would draw into it.

### ACPI

The address of the RSDP, not a copy of the tables. They stay where the firmware
put them and the kernel walks them itself. The loader has already checked that
the RSDP's checksums pass.

### Processors

What the MADT listed, with the flag saying which the firmware says may be
started. **The loader does not start them.** It reports them.

## Responses and memory

Every response is allocated in the loader's arena, which the memory map reports
as bootloader-reclaimable. A kernel may use that memory once it has copied
whatever it wants out of it, and not before — the responses, the entry arrays
and the strings all live there.

## Entry

The kernel is entered at its ELF entry point with paging already on and its own
tables loaded: its segments mapped where it asked for them, non-executable where
its program headers said so, plus an identity map of the first four gigabytes.
Nothing is passed in registers. Everything the kernel is told, it asked for.
