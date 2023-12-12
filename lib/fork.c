// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800
#define PTE_PERM_COW(pte) (((physaddr_t) (pte) >> 11) & 1)

/*
// given virtual address va, return the PTE address of va.
physaddr_t *pte_address(void *va)
{
	return (physaddr_t *) ((0x3bd << 22) | (((uintptr_t) va >> 10) & 0x3ffffc));
}
*/

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	// Otherwise, things go wrong in syscalls.
	addr = ROUNDDOWN(addr, PGSIZE);

	// not a write error
	if (!(err & FEC_WR))
	{
		panic("pgfault: not a write error, err=%d.\n", err);
	}
	pte_t pte = uvpt[PGNUM(addr)];
	// not a COW page.
	if (!(PTE_PERM_COW(pte) && PTE_PERM_P(pte)))
	{
		panic("pgfault: not a COW page.\n");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	// allocate a new page and map it to PFTEMP.
	if ((r = sys_page_alloc(0, (void *) PFTEMP, 
		PTE_U | PTE_P | PTE_W)) < 0)
	{
		panic("pgfault: %e\n", r);
	}

	// copy the data from the old page to the new page
	memmove((void *) PFTEMP, addr, PGSIZE);

	// move the new page to the old page's address
	if ((r = sys_page_map(0, (void *)PFTEMP, 0, 
		addr, PTE_U | PTE_P | PTE_W)) < 0)
	{
		panic("sys_page_map: %e\n", r);
	}

	// unmap PFTEMP
	if ((r = sys_page_unmap(0, (void *) PFTEMP)) < 0)
	{
		panic("sys_page_unmap: %e\n", r);
	}
    return;
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	
	int r;

	// LAB 4: Your code here.
	void *va = (void *) (pn * PGSIZE);
	uintptr_t addr = (uintptr_t) va;
	pte_t pte = uvpt[PGNUM(addr)];

	// shared page.
	if (pte & PTE_SHARE)
	{
		// copy the mapping directly
		if ((r = sys_page_map(0, va, envid, va, pte & PTE_SYSCALL)) < 0)
			panic("duppage: %e\n", r);
	}

	// writable or copy on write.
	else if (PTE_PERM_W(pte) || PTE_PERM_COW(pte))
	{
		// map the page copy-on-write in envid's space.
		if ((r = sys_page_map(0, va, envid, va, PTE_P | PTE_U | PTE_COW)) < 0)
			panic("duppage: %e\n", r);
	
		// remap the page copy-on-write in its own address space.
		if ((r = sys_page_map(0, va, 0, va, PTE_P | PTE_U | PTE_COW)) < 0)
			panic("duppage: %e\n", r);
	}

	// read-only
	else 
	{
		if ((r = sys_page_map(0, va, envid, va, PTE_P | PTE_U)) < 0)
			panic("duppage: %e\n", r);
	}
	

	// panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	
	// LAB 4: Your code here.
	envid_t envid;
	uintptr_t addr;
	int r;
	extern void _pgfault_upcall();

	// install page fault handler
	set_pgfault_handler(pgfault);

	// create a child environment.
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		// cprintf("%04x: this is child\n", sys_getenvid());
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// parent
	for (addr = UTEXT; addr < UTOP; addr += PGSIZE)
	{
		pde_t pde = uvpd[PDX(addr)];

		if (!PTE_PERM_P(pde))
			continue;
		
		pte_t pte = uvpt[PGNUM(addr)];

		// cprintf("pde=%x\n", pde);
		
		// if this page is the exception stack.
		if (addr == UXSTACKTOP - PGSIZE)
		{
			if ((r = sys_page_alloc(envid, (void *) (addr), 
				PTE_U | PTE_P | PTE_W)) < 0)
			{
				panic("fork: %e\n", r);
			}
			continue;
		}

		// if this page is not present
		if (!PTE_PERM_P(pde) || !PTE_PERM_P(pte))
			continue;

		duppage(envid, PGNUM(addr));
	}

	// sets the user page fault entrypoint for the child.
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) 
		< 0)
	{
		panic("sys_env_set_pgfault_upcall: %e\n", r);
	}

	// mark the child runnable
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
	{
		panic("sys_env_set_status: %e\n", r);
	}

	return envid;
		
	
	panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
