
import pickle
from capdl.Spec import Spec
from capdl.Object import CNode, Endpoint, Frame, TCB, PML4, Untyped, IRQControl, PageDirectory, ASIDPool, DomainControl, ASIDControl
from capdl.Cap import Cap
from capdl.Allocator import ObjectAllocator, CSpaceAllocator, AddressSpaceAllocator
import os


CNODE_SIZE = 20
# NOTE: this guard_size must be ARCH_WORD_SIZE - cnode_size
GUARD_SIZE = 32-CNODE_SIZE

cnode_provider = CNode("cnode_provider", CNODE_SIZE)
cnode_roottask = CNode("cnode_roottask", CNODE_SIZE)

# The fake seL4_CapIRQControl provided to the fake roottask
ep = Endpoint("endpoint")

# The real(or a fake if the cnode_provider is started by a fake roottask) irq_control to be passed into the provider
irq_control = IRQControl('irq_control')
cap_irq_control = Cap(irq_control)
domain_control = DomainControl('domain')
cap_domain_control = Cap(domain_control)
asid_control = ASIDControl('asid_control')
cap_asid_control = Cap(asid_control)

ipc_provider_obj = Frame("ipc_provider_obj", 4096)
ipc_roottask_obj = Frame("ipc_roottask_obj", 4096)
vspace_provider = PageDirectory("vspace_provider")
vspace_roottask = PageDirectory("vspace_roottask")
asid_pool_roottask = ASIDPool('asid_pool')

tcb_provider = TCB("tcb_provider", ipc_buffer_vaddr=0x0, ip=0x0,
                   sp=0x0, elf="provider", prio=255, max_prio=255, affinity=0, init=[])
tcb_roottask = TCB("tcb_roottask", ipc_buffer_vaddr=0x0, ip=0x0,
                   sp=0x0, elf="roottask", prio=254, max_prio=254, affinity=0, init=[])

# bi_frame for the fake roottask, this frame will also be passed into the provided to get filled
bi_frame = Frame("bi_frame", 0x1000)
# the device untyped which you want to pass into the fake roottask
device = Untyped('device_untyped', paddr=0xf8001000)

cnode_provider["0x1"] = Cap(tcb_provider)
cnode_provider["0x2"] = Cap(cnode_provider, guard_size=GUARD_SIZE)
cnode_provider["0x3"] = Cap(vspace_provider)
cnode_provider["0x4"] = cap_irq_control
cnode_provider["0x5"] = Cap(ep, read=True, write=True, grant=True)
cnode_provider["0x6"] = Cap(tcb_roottask)
cnode_provider["0x7"] = Cap(cnode_roottask, guard_size=GUARD_SIZE)
cnode_provider["0x8"] = Cap(device, write=True, read=True, grant=True)
cnode_provider["0x9"] = Cap(bi_frame, read=True, write=True)

#  seL4_CapNull                =  0,
cnode_roottask["0x1"] = Cap(tcb_roottask)
#  seL4_CapInitThreadTCB       =  1,
cnode_roottask["0x2"] = Cap(cnode_roottask, guard_size=GUARD_SIZE)
#  seL4_CapInitThreadCNode     =  2,
cnode_roottask["0x3"] = Cap(vspace_roottask)
#  seL4_CapInitThreadVSpace    =  3,
cnode_roottask["0x4"] = Cap(ep, read=True, write=True, grant=True)
#  seL4_CapIRQControl          =  4,
cnode_roottask["0x5"] = cap_asid_control
#  seL4_CapASIDControl         =  5,
cnode_roottask["0x6"] = Cap(asid_pool_roottask)
#  seL4_CapInitThreadASIDPool  =  6,

# TODO: not yet implemented
#  seL4_CapIOPortControl       =  7,
#  seL4_CapIOSpace             =  8,

cnode_roottask["0x{:x}".format(9)] = Cap(bi_frame, read=True)
#  seL4_CapBootInfoFrame       =  9,
cnode_roottask["0x{:x}".format(10)] = Cap(
    ipc_roottask_obj, read=True, write=True)
#  seL4_CapInitThreadIPCBuffer = 10,
cnode_roottask["0x{:x}".format(11)] = cap_domain_control
#  seL4_CapDomain              = 11,

tcb_provider['cspace'] = Cap(cnode_provider, guard_size=GUARD_SIZE)
tcb_provider['vspace'] = Cap(vspace_provider)
tcb_provider['ipc_buffer_slot'] = Cap(ipc_provider_obj, read=True, write=True)

tcb_roottask['cspace'] = Cap(cnode_roottask, guard_size=GUARD_SIZE)
tcb_roottask['vspace'] = Cap(vspace_roottask)
tcb_roottask['ipc_buffer_slot'] = Cap(ipc_roottask_obj, read=True, write=True)

untyped_list = []
num_untyped = 20
size_untyped = 23
# setup untyped caps for the fake roottask
for i in range(0, num_untyped):
    temp = Untyped('untyped_provider_for_roottask_{}'.format(i),
                   size_bits=size_untyped)
    untyped_list.append(temp)
    cnode_provider["0x{:X}".format(
        12 + i)] = Cap(temp, read=True, write=True, grant=True)

stack_0_provider_obj = Frame("stack_0_provider_obj", 4096)
stack_1_provider_obj = Frame("stack_1_provider_obj", 4096)
stack_2_provider_obj = Frame("stack_2_provider_obj", 4096)
stack_3_provider_obj = Frame("stack_3_provider_obj", 4096)
stack_4_provider_obj = Frame("stack_4_provider_obj", 4096)
stack_5_provider_obj = Frame("stack_5_provider_obj", 4096)
stack_6_provider_obj = Frame("stack_6_provider_obj", 4096)
stack_7_provider_obj = Frame("stack_7_provider_obj", 4096)
stack_8_provider_obj = Frame("stack_8_provider_obj", 4096)
stack_9_provider_obj = Frame("stack_9_provider_obj", 4096)

# HACK: this stack is just for making unmodified capdl-loader happy, since
# fake roottask's entry point will be sel4_start and it will setup a
# stack.
stack_0_roottask_obj = Frame("stack_0_roottask_obj", 4096)

obj = set([
    irq_control,
    domain_control,
    asid_control,
    cnode_provider,
    cnode_roottask,
    asid_pool_roottask,
    ep,
    device,
    ipc_provider_obj,
    ipc_roottask_obj,
    stack_0_provider_obj,
    stack_0_roottask_obj,
    stack_1_provider_obj,
    stack_2_provider_obj,
    stack_3_provider_obj,
    stack_4_provider_obj,
    stack_5_provider_obj,
    stack_6_provider_obj,
    stack_7_provider_obj,
    stack_8_provider_obj,
    stack_9_provider_obj,
    vspace_provider,
    vspace_roottask,
    tcb_provider,
    tcb_roottask,
    bi_frame,
])
obj.update(untyped_list)

spec = Spec('aarch32')
spec.objs = obj

objects = ObjectAllocator()
objects.counter = len(obj)
objects.spec.arch = 'aarch32'
objects.merge(spec)

provider_alloc = CSpaceAllocator(cnode_provider)
roottask_alloc = CSpaceAllocator(cnode_roottask)

cspaces = {'provider': provider_alloc, 'roottask': roottask_alloc}

provider_addr_alloc = AddressSpaceAllocator(None, vspace_provider)
provider_addr_alloc._symbols = {
    'mainIpcBuffer': ([4096], [Cap(ipc_provider_obj, read=True, write=True)]),
    'stack': ([4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, ],
              [Cap(stack_0_provider_obj, read=True, write=True),
               Cap(stack_1_provider_obj, read=True, write=True),
               Cap(stack_2_provider_obj, read=True, write=True),
               Cap(stack_3_provider_obj, read=True, write=True),
               Cap(stack_4_provider_obj, read=True, write=True),
               Cap(stack_5_provider_obj, read=True, write=True),
               Cap(stack_6_provider_obj, read=True, write=True),
               Cap(stack_7_provider_obj, read=True, write=True),
               Cap(stack_8_provider_obj, read=True, write=True),
               Cap(stack_9_provider_obj, read=True, write=True),
               ])}
provider_addr_alloc.add_symbol_with_caps(
    'bi_frame', [0x1000], [Cap(bi_frame, read=True, write=True)])

roottask_addr_alloc = AddressSpaceAllocator(None, vspace_roottask)
roottask_addr_alloc._symbols = {
    'mainIpcBuffer': ([4096], [Cap(ipc_roottask_obj, read=True, write=True)]),
    'stack': (
        [4096], [Cap(stack_0_roottask_obj, read=True, write=True), ]
    )
}

# HACK: provider will set the fake roottask's bootifo frame address
# to the value below. to get a legitimate value for the bootinfo,
# the best way is to create a region symbol below in to region_symbols
# variable and compile, then you can inspect the binary to see where is
# the bi_frame mapped to, and get back here to set the address
# to the value then recompile.
roottask_bi_addr = 0xf8000
roottask_addr_alloc.add_region_with_caps(
    roottask_bi_addr, [0x1000], [Cap(bi_frame, read=True, write=True)]
)

# HACK: please use the same hack above to find a legitimate address for the ipc
# buffer, you need to find the address of symbol mainIpcBuffer from the roottask
# binary this time
roottask_ipc_addr = 0xf7000
roottask_addr_alloc.add_region_with_caps(
    roottask_ipc_addr, [0x1000], [Cap(ipc_roottask_obj, read=True, write=True)]
)

addr_spaces = {
    'provider': provider_addr_alloc,
    'roottask': roottask_addr_alloc,
}

# HACK(Abuse?): To pass info into provider, I use cap_symbols to pass the values.
# capdl_linker will generate cspace_<your_program>.c files which will have contents
# like seL4_Word untyped_start = 12; .... and will compile the cspace file with the
# provider. All the necessary info is passed into the provider in this way. note
# that for each variable to be passed into provider, you need to add a extern var
# in provider.c to read the value.
cap_symbols = {
    'provider':
    [
        ('untyped_start', 12),
        ('untyped_end', 12 + num_untyped),
        # this slot for saving the caller
        ('free_slot_1', 12 + num_untyped + 1),
        # this slot for saving the ipc cap
        ('free_slot_2', 12 + num_untyped + 2),

        ('num_untyped_provide', num_untyped),
        ('untyped_size_bit', size_untyped),
        ('roottask_bi_addr', roottask_bi_addr),
        ('roottask_ipc_addr', roottask_ipc_addr),

        # empty start and end for the 'roottask'
        ('empty_start', 13),
        ('empty_end', 13 + 25000),  # empty_end should be related to cnode_size
        ('cnode_size', CNODE_SIZE),

        ('irq_control', 4),
        ('syscall_ep', 5),
        ('roottask_tcb', 6),
        ('roottask_cnode', 7),

        ('device_untyped', 8),
        ('device_untyped_addr', 0xf8001000),

        ('provider_cnode', 2),
    ],
    'roottask':
    [],
}

region_symbols = {
    'provider': [
        ('stack', 65536, 'size_12bit'),
        ('mainIpcBuffer', 4096, 'size_12bit'),
        ('bi_frame', 4096, 'size_12bit')
    ],
    'roottask': [
        ('stack', 4096, 'size_12bit'),
        ('mainIpcBuffer', 4096, 'size_12bit'),
        ('bi_frame', 4096, 'size_12bit')
    ]
}

elfs = {
    'provider': {'passive': False, 'filename': 'provider.c'},
    'roottask': {'passive': False, 'filename': 'roottask.c'},
}


print(pickle.dumps((objects, cspaces, addr_spaces, cap_symbols, region_symbols, elfs)))
