# GNM runtime evidence provenance

Address-specific observations for the current upstream implementation belong here rather than
in generic runtime source. These are existing maintainer evidence summaries; inference and
temporary stand-in limitations retain their original scope. No executable C tokens change.

## GNM system calls

System call group: GNM (GPU commands), libSceGnmDriver. Graphics are rebuilt at the engine level
   (issue #39), so these calls answer what the engine expects back; the GPU is not emulated.

   sceGnmGetTheTessellationFactorRingBufferBaseAddress: the OpenOrbis homebrew SDK's GnmDriver.h
   only names it. The executable's only call (0x02ad3fb4) takes a 64-bit address and maps 128 KiB
   of direct memory there (sceKernelMapDirectMemory with that address as a hint, protection 0x33,
   alignment 0x10000) for the GPU's tessellation factors. The PS4 answers a fixed address of its
   own; the runtime answers one it reserves for this, below 2^40 and clear of the executable,
   modules and thread objects. It is the runtime's choice, not the PS4's value: the engine only
   maps it and hands it back to the GPU calls.

## sceGnmValidateCommandBuffers

sceGnmValidateCommandBuffers(count, buffers, sizes, constant buffers, constant sizes): asks the GPU
   driver's debug validation layer to check command buffers. Paleblood has no such layer: the GPU is
   not emulated, and the command buffers are not what it draws from. The executable's start-up probe
   (0x02abb8e0, called from the graphics set-up at 0x0256b0f0) validates a tiny 16-byte buffer and
   treats exactly one result, 0x80d13fff, as "validation is not available"; the set-up then keeps
   the engine's validation option off (it stores the probe's answer AND its configuration flag). The
   runtime gives that answer for every call, which is true of Paleblood and is the value the game's
   own code names; no buffer is read or written. That a retail console answers the same is an
   inference (the probe exists so retail builds run without validation), not an observation.

## Default hardware state

sceGnmDispatchInitDefaultHardwareState and sceGnmDrawInitDefaultHardwareState200(command buffer,
   dwords available): write the packets that put the GPU's compute or graphics state in its default,
   and return how many dwords they wrote. The engine's wrappers (0x01489c50, five callers including
   the graphics set-up at 0x02ab1130; 0x01472870, seven callers) reserve 256 dwords, pass the cursor
   and the room left, and advance their cursor by the count; nothing checks the count or reads the
   packets back: only the GPU does. Paleblood has no GPU to set up (graphics are rebuilt at the engine
   level, issue #39, and the renderer sets its own state), so these write nothing and answer 0
   dwords, which is what they did.

## sceGnmSetCsShader

sceGnmSetCsShader(command buffer, dwords, shader): binds a compute shader. The engine's wrapper
   (0x0148a870, four callers including the set-up at 0x02ab4230) reserves 25 dwords, passes 25 and
   advances by 25 dwords only when the answer is 0; nothing else reads the answer.

## Other command-buffer writers

The other command-buffer writers the engine calls, each through one thin wrapper that reserves a
   fixed or computed number of dwords at its cursor, passes the cursor and that count first, and
   advances the cursor by the count (when the call answers 0, or always). OpenOrbis' GnmDriver.h
   only names them; the shapes come from the wrappers:
     shader stages (the shader's registers): SetEsShader 0x01475560 (20 dwords), SetGsShader
       0x014759d0 (29), SetHsShader 0x014755d0 (30), SetLsShader 0x014757d0 (23), SetPsShader
       0x014753c0 (40), SetVsShader 0x01475480 (29), and UpdateGsShader 0x01475a30, UpdateHsShader
       0x01475640, UpdatePsShader 0x01475420, UpdateVsShader 0x014754f0 with the same counts;
     debug markers, for GPU debugging tools (counts from the label's length): InsertPushMarker
       0x01477fe0, InsertPopMarker 0x014780e0 (6), InsertSetMarker 0x01477470 and 0x0148ae10,
       InsertPushColorMarker 0x01478060 and 0x0148b720, InsertSetColorMarker 0x014774f0 and
       0x0148ae90;
     InsertWaitFlipDone 0x01478440 (7): makes the GPU wait until the display no longer shows a
       buffer before drawing into it.
   Paleblood has no GPU to hold shader registers, no GPU debugger to read markers, and no GPU that
   could draw into a buffer early, so each fills its reserved space with one PM4 NOP packet and
   answers 0, as sceGnmSetCsShader does: the command buffer stays well-formed and nothing in it
   acts. The arguments after the count are not used.

## sceGnmIsUserPaEnabled

sceGnmIsUserPaEnabled(): whether the GPU's user performance-analysis mode is on. The engine wraps
   it as "on when nonzero" (0x01489c40) and asks from many draw-command builders (0x01474b50 and
   others), which then add the extra packets that mode needs. Paleblood has no such GPU mode, so the
   answer is 0, off; that a retail console also answers off is an inference (it is a development
   feature), not an observation.

## sceGnmMapComputeQueue

sceGnmMapComputeQueue(pipe, queue, ring, ring size in dwords, read pointer): OpenOrbis' GnmDriver.h
   only names it, so the shape comes from the engine's wrapper (0x01489a80, called from its compute
   queue setup at 0x0148c7b0). The wrapper passes the pipe and queue from the queue object, the ring
   buffer, its size in dwords and the address of the GPU's read pointer; a negative result is
   failure, and otherwise the wrapper keeps the result as the queue's id and sets the read pointer
   to 0. The setup then reports success only when that id is not 0. The runtime does not run the
   GPU, so the ring is never read: it records each queue for the engine-level graphics layer and
   answers ids from 1. Mapping the same pipe and queue twice and a missing ring or read pointer
   stop by name.

## sceGnmDingDong

sceGnmDingDong(queue id, write offset in dwords): tells the GPU that the ring of a compute queue
   holds new work up to the write offset. OpenOrbis' GnmDriver.h only names it; the shape comes from
   the engine's compute submit (0x0148c830), which copies one 4-dword INDIRECT_BUFFER packet per
   command buffer into the ring (the packet writer at 0x0148b330), wraps to the ring's start when
   the end is full, then rings the doorbell with the queue id from sceGnmMapComputeQueue and the new
   write offset. Before writing, it checks the free space against the read pointer the GPU advances.

   TEMPORARY STAND-IN (maintainer decision, 4 November 2026): Paleblood has no GPU, and the engine's
   own renderer is to replace this layer (issue #39). Until then the doorbell completes the work at
   once, on the calling thread: it follows each indirect buffer and acts only on the packets whose
   effect the CPU can see. A boot on 4 November found four kinds in the engine's compute buffers
   (packet types from AMD's public PM4 packet format):
     NOP and SET_SH_REG      skipped: padding and shader registers, with no GPU to hold them;
     DISPATCH_DIRECT         skipped: compute shaders do not run, so their output is never written;
     RELEASE_MEM             performed: the "work done" value goes to its memory address, which is
                             what the CPU waits on.
   Then the read pointer moves to the write offset, so the ring never fills. Any other packet, or a
   completion this stand-in does not perform, stops by name.

## sceGnmSubmitCommandBuffers

sceGnmSubmitCommandBuffers(count, draw command buffers, their sizes in bytes, constant command
   buffers, their sizes): hands command buffers to the GPU's graphics queue. OpenOrbis' GnmDriver.h
   only names it; the shape comes from the engine's submit (0x02ab0b00, one of eleven callers),
   which passes one or two draw command buffers, their byte sizes rounded down to whole dwords, and
   no constant buffers, and ignores the answer.

   TEMPORARY STAND-IN, as for sceGnmDingDong above: the work is completed at once on the calling
   thread. The first boot to reach it (4 November 2026) found, besides the compute kinds, these
   (AMD's public PM4 format):
     EVENT_WRITE_EOP              performed: the end-of-work value goes to its memory address;
     WAIT_REG_MEM                 checked: the memory must already hold what the GPU waits for;
     ACQUIRE_MEM, EVENT_WRITE     skipped: cache control and pipeline events, with no GPU caches
                                  (an EVENT_WRITE that also writes memory stops);
     SET_CONTEXT_REG, SET_UCONFIG_REG, NUM_INSTANCES, DRAW_INDEX_AUTO
                                  skipped: draw state and draws (the first frame's).
   Nothing is drawn. Constant command buffers, and any other packet, stop by name.

## sceGnmSubmitAndFlipCommandBuffers

sceGnmSubmitAndFlipCommandBuffers(count, draw command buffers, sizes, constant command buffers,
   sizes, video out handle, buffer index, flip mode, flip argument): submits a frame's last command
   buffers and shows one of the display's buffers once they are done. OpenOrbis' GnmDriver.h only
   names it; the shape comes from the engine's frame end (0x02ab0e30 and 0x02ab0ce0), which passes
   one or two draw command buffers, no constant buffers, and the port, buffer, mode and argument of
   its frame (the flip loop calling at 0x02ad6ee9), then calls sceGnmSubmitDone. The stand-in completes the buffers at once,
   as sceGnmSubmitCommandBuffers does, then flips as sceVideoOutSubmitFlip does.

## sceGnmSubmitDone

sceGnmSubmitDone(void): tells the system the frame's submissions are finished, after each
   submit-and-flip (0x02ab0e30, 0x02ab0ce0), which ignore the answer. The stand-in has already
   finished all submitted work, so there is nothing left to do; it answers 0.
