
import pickle
from capdl.Spec import Spec
from capdl.Object import CNode, Endpoint, Frame, TCB, PML4, Untyped, IRQControl, PageDirectory
from capdl.Cap import Cap
from capdl.Allocator import ObjectAllocator, CSpaceAllocator, AddressSpaceAllocator


CNODE_SIZE=8
# NOTE: this guard_size must be 64 - cnode_size
GUARD_SIZE=32-CNODE_SIZE

cnode_provider = CNode("cnode_provider", CNODE_SIZE)
cnode_roottask = CNode("cnode_roottask", CNODE_SIZE)
ep = Endpoint("endpoint")

cap_irq_control = Cap(IRQControl('irq_control'))

cnode_provider["0x1"] = Cap(ep, read=True, write=True, grant=True)
cnode_provider["0x2"] = Cap(cnode_provider, guard_size=GUARD_SIZE)
cnode_provider["0x3"] = cap_irq_control

cnode_roottask["0x1"] = Cap(ep, read=True, write=True, grant=True)
cnode_roottask["0x2"] = Cap(cnode_roottask, guard_size=GUARD_SIZE)
#  cnode_roottask["0x3"] = cap_irq_control

ipc_provider_obj = Frame ("ipc_provider_obj", 4096)
ipc_roottask_obj = Frame ("ipc_roottask_obj", 4096)
vspace_provider = PageDirectory("vspace_provider")
vspace_roottask = PageDirectory("vspace_roottask")

tcb_provider = TCB ("tcb_provider",ipc_buffer_vaddr= 0x0,ip= 0x0,sp= 0x0,elf= "provider",prio= 252,max_prio= 252,affinity= 0,init= [])
tcb_roottask = TCB ("tcb_roottask",ipc_buffer_vaddr= 0x0,ip= 0x0,sp= 0x0,elf= "roottask",prio= 252,max_prio= 252,affinity= 0,init= [])

tcb_provider['cspace'] = Cap(cnode_provider, guard_size=GUARD_SIZE)
tcb_provider['vspace'] = Cap(vspace_provider)
tcb_provider['ipc_buffer_slot'] = Cap(ipc_provider_obj, read=True, write=True)

tcb_roottask['cspace'] = Cap(cnode_roottask, guard_size=GUARD_SIZE)
tcb_roottask['vspace'] = Cap(vspace_roottask)
tcb_roottask['ipc_buffer_slot'] = Cap(ipc_roottask_obj, read=True, write=True)

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

stack_0_roottask_obj = Frame("stack_0_roottask_obj", 4096)
stack_1_roottask_obj = Frame("stack_1_roottask_obj", 4096)
stack_2_roottask_obj = Frame("stack_2_roottask_obj", 4096)
stack_3_roottask_obj = Frame("stack_3_roottask_obj", 4096)
stack_4_roottask_obj = Frame("stack_4_roottask_obj", 4096)
stack_5_roottask_obj = Frame("stack_5_roottask_obj", 4096)
stack_6_roottask_obj = Frame("stack_6_roottask_obj", 4096)
stack_7_roottask_obj = Frame("stack_7_roottask_obj", 4096)
stack_8_roottask_obj = Frame("stack_8_roottask_obj", 4096)
stack_9_roottask_obj = Frame("stack_9_roottask_obj", 4096)

obj = set([
    cnode_provider,
    cnode_roottask,
    ep,
    ipc_provider_obj,
    ipc_roottask_obj,
    stack_0_provider_obj,
    stack_0_roottask_obj,
    stack_1_provider_obj,
    stack_1_roottask_obj,
    stack_2_provider_obj,
    stack_2_roottask_obj,
    stack_3_provider_obj,
    stack_3_roottask_obj,
    stack_4_provider_obj,
    stack_4_roottask_obj,
    stack_5_provider_obj,
    stack_5_roottask_obj,
    stack_6_provider_obj,
    stack_6_roottask_obj,
    stack_7_provider_obj,
    stack_7_roottask_obj,
    stack_8_provider_obj,
    stack_8_roottask_obj,
    stack_9_provider_obj,
    stack_9_roottask_obj,
    vspace_provider,
    vspace_roottask,
    tcb_provider,
    tcb_roottask,
])
spec = Spec('aarch32')
spec.objs = obj

objects = ObjectAllocator()
objects.counter = len(obj)
objects.spec.arch  = 'aarch32'
objects.merge(spec)

provider_alloc = CSpaceAllocator(cnode_provider)
provider_alloc.slot = 4
roottask_alloc = CSpaceAllocator(cnode_roottask)
roottask_alloc.slot = 4
cspaces = {'provider':provider_alloc, 'roottask': roottask_alloc}


provider_addr_alloc = AddressSpaceAllocator(None, vspace_provider)
provider_addr_alloc._symbols = {
    'mainIpcBuffer': ([4096], [Cap(ipc_provider_obj, read=True, write=True)]),
    'stack': ([4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096,],
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

roottask_addr_alloc = AddressSpaceAllocator(None, vspace_roottask)
roottask_addr_alloc._symbols = {
    'mainIpcBuffer': ([4096], [Cap(ipc_roottask_obj, read=True, write=True)]),
    'stack': ([4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096,],
              [Cap(stack_0_roottask_obj, read=True, write=True),
               Cap(stack_1_roottask_obj, read=True, write=True),
               Cap(stack_2_roottask_obj, read=True, write=True),
               Cap(stack_3_roottask_obj, read=True, write=True),
               Cap(stack_4_roottask_obj, read=True, write=True),
               Cap(stack_5_roottask_obj, read=True, write=True),
               Cap(stack_6_roottask_obj, read=True, write=True),
               Cap(stack_7_roottask_obj, read=True, write=True),
               Cap(stack_8_roottask_obj, read=True, write=True),
               Cap(stack_9_roottask_obj, read=True, write=True),
               ])}

addr_spaces = {
    'provider': provider_addr_alloc,
    'roottask': roottask_addr_alloc,
}

cap_symbols = {
    'provider':
    [('endpoint', 1),
     ('cnode', 2),
     ('badged_endpoint', 3)],
    'roottask':
    [('endpoint', 1),
     ('cnode', 2),
     ('badged_endpoint', 3)],
}

region_symbols = {
    'provider': [('stack', 65536, 'size_12bit'), ('mainIpcBuffer', 4096, 'size_12bit')],
    'roottask': [('stack', 65536, 'size_12bit'), ('mainIpcBuffer', 4096, 'size_12bit')]
}

elfs =  {
    'provider': {'passive': False, 'filename': 'provider.c'},
    'roottask': {'passive': False, 'filename': 'roottask.c'},
}

print(pickle.dumps((objects, cspaces, addr_spaces, cap_symbols, region_symbols, elfs)))
