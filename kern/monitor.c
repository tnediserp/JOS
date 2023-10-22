// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "prints a backtrace of the stack: a list of the saved Instruction Pointer (IP) values from the nested call instructions that led to the current point of execution.", mon_backtrace},
	{"showmapping", "show the virtual address and physical address mapping.",
	mon_showmapping},
	{"clearperm", "clear permissions of a virtual page.", mon_clearperm},
	{"setperm", "set permissions of a virtual page.", mon_setperm},
	{"memdump", "dump the N words of memory starting at start_mem.", 
	mon_dumpmem}
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp, *ebp_add;
	uint32_t eip, arg1, arg2, arg3, arg4, arg5;
	struct Eipdebuginfo info;

	ebp = read_ebp();

	cprintf("Stack backtrace:\n");
	while (ebp != 0)
	{
		ebp_add = (uint32_t *) ebp;

		eip = *(ebp_add + 1);
		arg1 = *(ebp_add + 2);
		arg2 = *(ebp_add + 3);
		arg3 = *(ebp_add + 4);
		arg4 = *(ebp_add + 5);
		arg5 = *(ebp_add + 6);

		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n", ebp,
		 	eip, arg1, arg2, arg3, arg4, arg5);

		debuginfo_eip(eip, &info);

		cprintf("         %s:%d: %.*s+%d\n", info.eip_file, info.eip_line, 
		info.eip_fn_namelen, info.eip_fn_name, eip - info.eip_fn_addr);

		ebp = *ebp_add;
	}
	
	return 0;
}

/*
static size_t
strtoi(char *s)
{
	// hexa
	size_t x = 0;
	char *p;
	if (s[0] == '0' && s[1] == 'x')
	{
		for (p = s+2; *p != 0; p++)
		{
			if (*p >= '0' && *p <= '9')
				x = x * 16 + *p - '0';
			else if (*p >= 'a' && *p <= 'f')
				x = x * 16 + *p - 'a' + 10;
			else if (*p >= 'A' && *p <= 'F')
				x = x * 16 + *p - 'A' + 10;
		}
	}
	else 
	{
		for (p = s; *p != 0; p++)
		{
			x = x * 10 + *p - '0';
		}
	}
	return x;
}
*/

extern pte_t *
pgdir_walk(pde_t *pgdir, const void *va, int create);

extern pte_t *kern_pgdir;

// Display the pte information. 
// Print physical address and permissions.
// If va is not mapped to a physical page, do nothing and return NULL.
static pte_t *
pte_info_display(pte_t *pgdir, void *va)
{
	pte_t *pte = pgdir_walk(pgdir, va, 0);
	cprintf("va: 0x%x, ", va);
	if (pte == NULL)
	{
		cprintf("page does not exist.\n");
		return NULL;
	}
		
	else 
	{
		if (!(*pte & PTE_P))
		{
			cprintf("page does not exist.\n");
			return NULL;
		}
		
		else 
			cprintf("pa: 0x%x, PTE_P: %x, PTE_W: %x, PTE_U: %x\n",
			PTE_ADDR(*pte), PTE_PERM_P(*pte), PTE_PERM_W(*pte), 
			PTE_PERM_U(*pte));
	}
	return pte;
}

// Print the pte information in virtual address range [start, end).
static void 
page_region_display(void *start, void *end)
{
	start = ROUNDDOWN(start, PGSIZE);
	end = ROUNDUP(end, PGSIZE);
	void *va;

	for (va = start; va < end; va += PGSIZE)
	{
		pte_info_display(kern_pgdir, va);
	}
}

// show the virtual address and physical address mapping.
int 
mon_showmapping(int argc, char *argv[], struct Trapframe *tf)
{
	void *start, *end;
	if (argc == 2)
	{
		start = (void *) strtol(argv[1], NULL, 0);
		page_region_display(start, start + PGSIZE);
	}
	else if (argc ==3)
	{
		start = (void *) strtol(argv[1], NULL, 0);
		end = (void *) strtol(argv[2], NULL, 0);
		page_region_display(start, end);
	}
	else 
	{
		cprintf("useage: showmapping [start_addr] [end_addr]\n\
		showmapping [addr]\n");
	}
	return 0;
}

// clear permissions of a virtual page.
int
mon_clearperm(int argc, char *argv[], struct Trapframe *tf)
{
	if (argc != 2)
	{
		cprintf("usage: clearperm [virtual address]\n");
		return 0;
	}
	void *va = (void *) strtol(argv[1], NULL, 0);
	
	pte_t *pte = pte_info_display(kern_pgdir, va);
	if (pte)
	{
		*pte &= ~0x6;
		cprintf("permissions cleared.\n");
		pte_info_display(kern_pgdir, va);
	}
	return 0;
}

// set permissions of a virtual page.
int 
mon_setperm(int argc, char *argv[], struct Trapframe *tf)
{
	if (argc != 4)
	{
		cprintf("usage: setperm [virtual address] [r/w] [u/k]\n");
		return 0;
	}
	int perm_W, perm_U;
	
	if ((strlen(argv[2]) == 1) && argv[2][0] == 'r')
		perm_W = 0;
	else if ((strlen(argv[2]) == 1) && argv[2][0] == 'w') 
		perm_W = PTE_W;
	else 
	{
		cprintf("usage: setperm [virtual address] [r/w] [u/k]\n");
		return 0;
	}

	if ((strlen(argv[3]) == 1) && argv[3][0] == 'u')
		perm_U = PTE_U;
	else if ((strlen(argv[3]) == 1) && argv[3][0] == 'k')
		perm_U = 0;
	else 
	{
		cprintf("usage: setperm [virtual address] [r/w] [u/k]\n");
		return 0;
	}

	void *va = (void *) strtol(argv[1], NULL, 0);
	
	pte_t * pte = pte_info_display(kern_pgdir, va);
	if (pte)
	{
		*pte &= ~0x6;
		*pte |= (perm_U | perm_W);
		cprintf("permissions set.\n");
		pte_info_display(kern_pgdir, va);
	}

	return 0;
}

// Dump the content of virtual address in range [start, end).
// 1 line corresponds to 4 Bytes.
// Here start and end are assumed to be a multiple of 4.
void
dumpvm(size_t *start, size_t *end)
{
	size_t *va;
	for (va = start; va < end; va++)
	{
		pte_t *pte = pgdir_walk(kern_pgdir, va, 0);
		if (pte)
		{
			physaddr_t pa = PTE_ADDR(*pte) | PGOFF(va);
			cprintf("va: 0x%08x, pa: 0x%08x, content: 0x%08x\n", va, pa, *va);
		}
		else
		{
			cprintf("va: 0x%08x, pa: none, content: none\n", va);
		}
	}
}

/*
void
dumppm(physaddr_t start, physaddr_t end)
{
	physaddr_t pa;
	for (pa = start; pa < end; pa += 4)
	{
		size_t *va = (size_t *) KADDR(pa);
		cprintf("va: 0x%08x, pa: 0x%08x, content: 0x%08x\n", va, pa, *va);
	}
}
*/

// Dump the content of physical address in range [start, end).
// 1 line corresponds to 4 Bytes.
// Here start and end are assumed to be a multiple of 4.
void
dumppm(physaddr_t start, physaddr_t end)
{
	void *mapping = 0;
	physaddr_t pa = start;
	while (pa < end)
	{
		page_insert(kern_pgdir, pa2page(pa), mapping, PTE_P);
		physaddr_t nextpg = ROUNDUP(pa + 1, PGSIZE);
		for (; pa < nextpg && pa < end; pa += 4)
		{
			physaddr_t offset = pa - ROUNDDOWN(pa, PGSIZE);
			cprintf("pa: 0x%08x, content: 0x%08x\n", pa, 
			*((size_t *)(mapping + offset)));
		}
	}
	page_remove(kern_pgdir, mapping);
}

// dump the N words of memory starting at start_mem.
// 1 word is assumed to be 4 Bytes.
int 
mon_dumpmem(int argc, char *argv[], struct Trapframe *tf)
{
	if (argc != 4)
	{
		cprintf("usage: memdump [v/p] [start_mem] [N]\n");
		return 0;
	}

	size_t *start = (size_t *) ROUNDDOWN(strtol(argv[2], NULL, 0), 4);
	size_t *end = start + strtol(argv[3], NULL, 0);
	if ((strlen(argv[1]) == 1) && argv[1][0] == 'v')
	{
		dumpvm(start, end);
	}
	else if ((strlen(argv[1]) == 1) && argv[1][0] == 'p')
	{
		dumppm((physaddr_t) start, (physaddr_t) end);
	}
	else 
	{
		cprintf("usage: memdump [v/p] [start_mem] [N]\n");
	}
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	int x = 1, y = 3, z = 4;
	cprintf("x %d, y %x, z %d\n", x, y, z);

	unsigned int i = 0x00646c72;
 	cprintf("H%x Wo%s\n", 57616, &i);

	cprintf("x=%d y=%d\n", 3);



	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
