---
layout: post
title: "Understanding the Haskell FFI"
date: 2013-09-07 19:54
comments: false
categories: [haskell, ffi, programming, c]
---

*This article is based on a talk I originally gave at the*
[St. Louis Lambda Lounge](http://lambdalounge.org)
*in February of 2013.  The original slides are*
[available here](/assets/haskell_ffi/understanding_the_haskell_ffi.pdf)

## Table of Contents

1. [Introduction](#intro)
1. [Symbols, Linking, and ABIs](#abi)
1. [Native and Haskell Types](#types)
1. [Functions](#functions)
1. [Using hsc2hs](#hsc)
1. [Guidelines](#guidelines)
1. [References](#refs)

## <a id="intro">Introduction</a>

### What is the FFI?

The haskell Foreign Function Interface (FFI) is the method by which haskell
code may interact with the native code on a system.  In other words, whether
you wish to utilize existing code written in native languages for your platform,
or provide code written in Haskell to other applications as a library, the FFI
is the mechanism by which your haskell code interoperates with native code.
The FFI is not a part of the
[Haskell 98 Standard](http://www.haskell.org/onlinereport/).  It is defined in
the [Haskell 98 FFI Addendum](http://www.cse.unsw.edu.au/~chak/haskell/ffi/ffi/ffi.html).

In this article I will first discuss the difference between native and haskell
code and why the FFI is necessary for interoperability.  I will talk about
binary file formats such as ELF, the purpose of ABIs and calling conventions,
and the mechanics of how objects are linked together statically and dynamically
to create running programs.  With that background in place I will talk about
the practical concerns of types, with an emphasis on the underlying mechanics
of data marshalling and how to retain type safety in your haskell application
when interacting with native libraries.  Next I will discuss the methods
defined by the FFI for creating foreign declarations of imported or exported
symbols and how to functions and shared data.  With the low level background in
place I will talk about the hsc2hs utility for generating haskell bindings for
C libraries. I will conclude with a set of tips and guidelines on using the
FFI.

<!-- more -->

### Terminology

During the course of this article I will use the terms `system` and `native`
with specific definitions more narrow than in general parlance.  Specifically
I will define a `system` as *a unique operating environment consisting of an
operating system, executable and shared library format, ABI, CPU architecture
and instruction set, and userspace API*.  We will use the term `native` to
refer to *executable programs or shared libraries which conform to the CPU
architecture and instruction set, ABI, calling conventions, and binary file
format of the host system, or source code that will be compiled to generate
them*.  

### Our Reference Platform

Dealing with the FFI inherently involves dealing with the underlying details of
your chosen platform.  In order to avoid getting mired in ambiguity I've chosen
to select a reference platform that will be used for all of the examples in
this article.  Throughout the article I will attempt to make note of instances
where I rely on platform specific behaviors, however I will not generally
bother to provide multiple examples from different systems, and may at times in
examples use language that refers to implementation details of the sample
system.  The specific reference platform I am using is an
[Ubuntu Linux 12.10](http://ubuntu.com) system with [gcc](http://gcc.gnu.org)
version 4.7.2, [ghc](http://haskell.org/ghc) version 7.4.2,
[EGLIBC](http://eglibc.org) version 2.15, on an AMD64 compatible CPU.  This
reference platform uses the 
[AMD64 System V ABI](http://www.uclibc.org/docs/psABI-x86_64.pdf) and the
[ELF64](http://www.skyfree.org/linux/references/ELF_Format.pdf) binary file
format.

### About the Examples

#### Haskell Examples

Except where otherwise specified, all of the Haskell examples provided in this
article were compiled and tested with GHC 7.4.2.  For the examples that are
using the FFI you must either specify the FFI pragma at the top of your source
code, or pass either the `-XForeignFunctionInterface` or `-fglasgow-exts` flags
to ghc.

{% codeblock The Haskell FFI Pragma lang:haskell %}
{-# LANGAUGE ForeignFunctionInterface #-}
{% endcodeblock %}

#### C Examples

All of the C examples provided in this article were compiled and tested with
GCC version 4.7.2.  In the interest of conciseness I have in some places used
GNU specific extensions.  Except where otherwise specified, all of the sample
code was compiled with

`gcc -std=gnu99 -O0 -Wall -Wextra -Werror`

*Note that all of the examples provided here have been selected for simplicity
and readability as example code and may contain any number of memory leaks,
buffer overflow errors, reliance on implementation defined behavior, etc. and
should in no case be used in a production environment.*

## <a id="abi">Symbols, Linking, and ABIs</a>

In order to understand what the FFI is doing you need to first understand how
programs are actually executed.  Although I will focus on the implementation
details of our target system in this section, many of the concepts that are
covered are widely applicable to all modern systems, with some minor
differences in the underlying implementation details.

At the core of the way applications are executed on a modern system are three
key concepts:

1. The Binary Format
1. The Dynamic Linker
1. The Application Binary Interface (ABI)

Here I will briefly discuss the ELF file format used for applications, object
files, and shared objects on our target system and the static and dynamic
linking phases of applications on our target system, and examine the way that
function calls are handled through our systems ABI.  This is not intended to be
a full treatment on these subjects, instead I wish only to provide a general
mental framework for how these parts of the system work to aid in building a
sound mental model of the FFI and to aid in debugging.  You should reference
the linked documentation for more complete understanding of how these aspects
of the system work.  If you aren't interested in the level of detail provided
here, you can skip to the <a id="#linking_summary">summary</a> to get a
synopsis of this section.

### A Working Example
<a id="figure_1.1"></a>

{% include_code Figure 1.1 lang:c haskell_ffi/hello_c.c %}

To begin our examination of the ELF format we will use a small C program
([Figure 1.1](#figure_1.1)).  This program has two functions, `main`,
the entry point to our application, and `generate_message`, which takes as a
parameter a string, allocates a new string on the heap, writes "_Hello,
_\<name\>" into the string, then returns a pointer to the allocated string.

The first thing we should do is compile our sample program into an object file
. Once we have compiled the program to an ELF object file, we can examine the
contents of the object file using the `readelf` command
([Figure 1.2](#figure_1.2)).

 _For the sake of brevity, the output of_ `readelf` _has been omitted.  For
reference you may download the entire output used in the rest of this section_
[here](/downloads/code/haskell_ffi/hello_c.elfdata.txt)

<a id="figure_1.2"></a>
{% codeblock Figure 1.2 lang:text %}
rebecca@laptop$ gcc -std=gnu99 -O0 -c hello_c.c -o hello_c.o
rebecca@laptop$ readelf -a hello_c.o
{% endcodeblock %}

### The Structure of an ELF Object File

The [ELF File Format](http://www.skyfree.org/linux/references/ELF_Format.pdf),
in the abstract, consists of an ELF file header, followed by an optional
program header and 1 or more sections, followed by a section header table.  We
can look at the GNU binutils implementation of the ELF file format to get a
better idea of what these data structures look like ([Figure 2.1](#figure_2.1))

<a id="figure_2.1"></a>
{% codeblock Figure 2.1 lang:c %}
/* ELF64 File Header */
typedef struct
{
  unsigned char e_ident[EI_NIDENT];  /* Magic number and other info */
  Elf64_Half    e_type;              /* Object file type */
  Elf64_Half    e_machine;           /* Architecture */
  Elf64_Word    e_version;           /* Object file version */
  Elf64_Addr    e_entry;             /* Entry point virtual address */
  Elf64_Off     e_phoff;             /* Program header table file offset */
  Elf64_Off     e_shoff;             /* Section header table file offset */
  Elf64_Word    e_flags;             /* Processor-specific flags */
  Elf64_Half    e_ehsize;            /* ELF header size in bytes */
  Elf64_Half    e_phentsize;         /* Program header table entry size */
  Elf64_Half    e_phnum;             /* Program header table entry count */
  Elf64_Half    e_shentsize;         /* Section header table entry size */
  Elf64_Half    e_shnum;             /* Section header table entry count */
  Elf64_Half    e_shstrndx;          /* Section header string table index */
} Elf64_Ehdr;

/* ELF64 Section Header */
typedef struct
{
  Elf64_Word    sh_name;             /* Section name (string tbl index) */
  Elf64_Word    sh_type;             /* Section type */
  Elf64_Xword   sh_flags;            /* Section flags */
  Elf64_Addr    sh_addr;             /* Section virtual addr at execution */
  Elf64_Off     sh_offset;           /* Section file offset */
  Elf64_Xword   sh_size;             /* Section size in bytes */
  Elf64_Word    sh_link;             /* Link to another section */
  Elf64_Word    sh_info;             /* Additional section information */
  Elf64_Xword   sh_addralign;        /* Section alignment */
  Elf64_Xword   sh_entsize;          /* Entry size if section holds table */
} Elf64_Shdr;
{% endcodeblock %}

The `Elf64_Ehdr` defines the structure for the ELF file header.  Of particular
interest to us are the `e_shoff` and `e_shnum` fields, which give the offset of
and number of entries into the section header table.  The section table
provides the addresses for the sections of the ELF file.
[Figure 2.1](#figure_2.1) contains a sample section table from the ELF object
file generated in [1.2](#figure_1.2).

<a id="figure_2.1"></a>
{% codeblock Figure 2.1 lang:text %}
Section Headers:
  [Nr] Name              Type        Address           Offset Size              EntSize          Flags  Link  Info  Align
  [ 0]                   NULL        0000000000000000  00000000 0000000000000000  0000000000000000           0     0     0
  [ 1] .text             PROGBITS    0000000000000000  00000040 0000000000000070  0000000000000000  AX       0     0     4
  [ 2] .rela.text        RELA        0000000000000000  00000678 0000000000000090  0000000000000018          11     1     8
  [ 3] .data             PROGBITS    0000000000000000  000000b0 0000000000000000  0000000000000000  WA       0     0     4
  [ 4] .bss              NOBITS      0000000000000000  000000b0 0000000000000000  0000000000000000  WA       0     0     4
  [ 5] .rodata           PROGBITS    0000000000000000  000000b0 0000000000000010  0000000000000000   A       0     0     1
  [ 6] .comment          PROGBITS    0000000000000000  000000c0 000000000000002b  0000000000000001  MS       0     0     1
  [ 7] .note.GNU-stack   PROGBITS    0000000000000000  000000eb 0000000000000000  0000000000000000           0     0     1
  [ 8] .eh_frame         PROGBITS    0000000000000000  000000f0 0000000000000058  0000000000000000   A       0     0     8
  [ 9] .rela.eh_frame    RELA        0000000000000000  00000708 0000000000000030  0000000000000018          11     8     8
  [10] .shstrtab         STRTAB      0000000000000000  00000148 0000000000000061  0000000000000000           0     0     1
  [11] .symtab           SYMTAB      0000000000000000  000004f0 0000000000000150  0000000000000018          12     9     8
  [12] .strtab           STRTAB      0000000000000000  00000640 0000000000000034  0000000000000000           0     0     1
{% endcodeblock %}

Looking at a sample section header table ([Figure 2.1](#figure_2.1)) from an
objected file generated from our sample code we see that that are several
sections that have been created.  Of particular interest to us are the:

1. `.text` section, which contains the executable program data
1. `.rela.text` section, which contains the relocations for the text section
1. `.data` section, which contains global variables, etc.
1. `.symtab` section, which contains the symbol table

Lets look at these particular sections individual to understand why they are
relevant to our understanding of program linking and execution using the FFI.

### The *.text* and *.rela.text* Sections

The *.text* section is the part of our object file that contains the actual
executable data in our object file as indicated by the `PROGBITS` value of the
`type` field in [Figure 2.1](#figure_2.1).  The *.rela.text* section contains
the relocations for the code in the *.text* section.

### The *.data* and *.rodata* Sections

The *.data* and *.rodata* sections contain variables used by the application.
*.data* contains global mutable variables that may be exported by the object
file.  *.rodata* contains read-only data, for example string literals used in
the application.  Symbols may be the names of variables, functions, ELF file
section names, etc.

### The Symbol Table: *.symtab*

The symbol table section contains the data for the symbol hash map that is used
at both link and runtime to resolve symbols referenced in the ELF file.  We can
see in [Figure 2.2](#figure_2.2) that a symbol contains, among other things, an
`Elf64_Addr` value and an `Elf64_Xword` size parameter.

<a id="figure_2.2"></a>
{% codeblock Figure 2.2 lang:c %}
typedef struct
{
  Elf64_Word    st_name;    /* Symbol name (string tbl index) */
  unsigned char st_info;    /* Symbol type and binding */
  unsigned char st_other;   /* Symbol visibility */
  Elf64_Section st_shndx;   /* Section index */
  Elf64_Addr    st_value;   /* Symbol value */
  Elf64_Xword   st_size;    /* Symbol size */
} Elf64_Sym;
{% endcodeblock %}

In the symbol table section of our generated ELF output a mapping is generated
between symbol names (ASCII strings representing the name of the symbol) and
the symbol value, containing the address and size of the symbol in the binary.

### Relocations

When an ELF file is generated the symbols contain references to addresses
within the binary, however at each stage of ELF file generation symbols must be
references when their final address is unknown.  Reasons for this include:

- The initial offset will be unknown on systems which use address space randomization
- Link-time optimizations may re-order or inline code to improve performance
- Symbols may be contained in shared objects with unknown offsets
- The order in which shared objects are loaded may affect symbol offsets
- Lazy binding of symbols may affect offsets

To solve these problems the ELF file format contains the notion of
relocations.  A relocation contains information that allows the location of a
symbol to be changed by the linker.  This is what allows static and dynamic
linking, link-time optimization, and lazy symbol binding, and more to function
in a modern system.

### <a id="linking_summary"></a> Summary 

The final result of compilation and linking is that we have a binary file
containing an ELF header and symbol tables, the symbol tables containing memory
addresses and offsets into the application address space where the symbols are
located.  During program execution we reference these symbols by performing a
lookup in the symbol table.  Functions available in the applications virtual
address space, either because they were statically linked into the executable
file or have been loaded by the runtime linker, have their entry point
addresses stored in the symbol table.  When a function is called, the compiler
generates code to put parameters into registers, generate a stack frame, etc.
as dictated by the system ABI, and then moves execution to the address
specified by the symbol or relocation address stored in the symbol table.  The
code executes and control is returned to the calling function again as per the
architecture ABI.

The purpose of understanding the details of program execution at this level is
not to enable general use of the FFI; the compiler and linker will take care of
these low level details and the programmer need not in general be concerned
with them.  Having a general understanding of these processes is however useful
when attempting to debug an application using the FFI, or any application that
is experiencing build or link errors.

## <a id="types">Native and Haskell Types</a>

Types are a core aspect of writing Haskell code, and may cause some amount of
difficult for a programmer who is attempting to interoperate with native code
written in a language with a signficiantly different type system.  With C
specifically, it quickly becomes clear that while both C and haskell offer type
systems, these type systems are abstracting over quite different ideas, and it
can be difficult at first to understand how to write correctly typed code that
does not simply forsake the type system of one of the two languages.

We will begin by looking briefly at the C type system.  Next we will look at
the types that are provided by the Haskell FFI and how they help us to
interoperate with C programs.  With a thorough understanding of how to deal
with native C types through the FFI we will look at how to user defined types
using opaque pointers and the `Storable` typeclass.  Finally we'll discuss some
general strategies for easing the process of dealing with types in programs
that use the FFI.

### The C Type System

Before we get started looking at the way the FFI maps C types to Haskell types
and vice-versa we should first look at the C type system.  For complete
information on the C type system see the _Types_ section of the language
standard.

The C standard defines three kinds of types:

1. Object Types
1. Function Types
1. Incomplete Types

We will start by looking at each of these kinds of types in the basic case,
and then we will look at *Derived* and *Qualified* types

#### Object Types

The term _Object Types_ is used to describe the set of types that fully
describe data on the system.  At compile time all object types must have a
_known constant size_.  The most commonly used object types are the _Basic
Types_, which collectively refers to the character, integer, and floating point
types.

The C language specification specifies a number of object types and then
creates many different type groupings based on properties of these types.  We
will not go into great detail and will instead give a brief overview of some of
the important object types.

##### The Character Types

The _Character Types_ refer to the `char`, `signed char`, and `unsigned char`
types.  The language specification requires that a `char` type be large enough
to hold any single character from the systems native character set.  The
character types are also _Standard Integer Types_.  Note that as per the
language specification, the signedness of `char` is implementation defined.
You may use `signed char` or `unsigned char` to ensure a signed or unsigned
value respectively when using a character type as an integer type.  On our
reference platform, `char` is a signed 1-byte value.

##### The Integer Types

The _Integer Types_ refer to the signed and unsigned integer types, consisting
of the following types and their unsigned equivalents (reference platform sizes
are given in parentheses):

- `char` (1 byte)
- `short int` (2 bytes)
- `int` (4 bytes)
- `long int` (8 bytes)
- `long long int` (8 bytes)

##### The Floating Point Types

The _Floating Point Types_ refer to the IEEE-754 floating point values shown;
(precision is given in parentheses)\[reference platform sizes are given in
brackets\]

- `float` (single precision) \[4 bytes\]
- `double` (double precsiion) \[8 bytes\]
- `long double` (extended precision _where available_) \[16 bytes\]

##### The Complex Types

The _Complex Types_ extend the real types to support complex numbers.  On our
reference platform, GCC supports complex integer types as an extension.  The
complex types represent complex numbers as shown below.

- `float _Complex` (single precision) \[8 bytes\]
- `double _Complex` (double precsiion) \[16 bytes\]
- `long double _Complex` (extended precision _where available_) \[32 bytes\]

##### Enumerated Types

An *Enumeration* is a collection of Integer values, and therefore has the same
storage characteristics as an `int`, however an enumerated type is not an
integer or arithmetic type.  On our reference platform an enumerated type has a
storage size of 4 bytes.

#### Function Types

In C a function type is distinct from an object type, and represents a
function.  Function types are derived from a functions return type and the
number and types of it's parameters.

#### Incomplete Types

The incomplete types are the set of types including `void`, and object or
derived types without a known constant size (including variable length arrays).
Incomplete types cannot be instantiated, but they may be used in derived types
or as part of a function type.

#### Derived Types

Derived types are constructed based on Object, Function, or Incomplete types.
The type or types used in the creation of a derived type are known as the
_Referenced Types_.

##### Array Types

An array type represents a nonzero set of elements of a single referenced type.
Arrays, along with structures, are known as _aggregate types_.  An array of a
specified fixed length has a known constant size and is therefore a complete
type.  A variable length array may be created, however it is an incomplete type
until a fixed size is specified for it.  Consider the example given in
*Figure 3.1*

{% codeblock Figure 3.1 lang:c %}
struct example_struct
{
    char letter;    /* Holds some arbitrary letter */
    int  numbers[]; /* Holds some arbitrary set of integers */
} e = {'a',{1,2,3,4}}; /* This is valid */

int main(int argc, char* argv[])
{
    /* this is a valid way of creating another struct */
    struct example_struct* f = malloc( sizeof(struct example_struct)
                                     + (3 * sizeof(int)));
    *f = e; /* Valid */
    /* Error: non-static initialization of flexible array member */
    struct example_struct g = {'c',{7,8,9}};
}
{% endcodeblock %}

##### Pointer Types

A pointer type is an object type and is a reference to referenced type.  A
pointer type with a referenced type of *T* is said to be a *Pointer to T*. A
pointer may reference an object, function, or incomplete type, including
derived types.

##### Structure Types

A structure type, along with an array type, is an *aggregate type*. A structure
type consists of a potentially heterogenous set of object types (and,
optionally, a single incomplete array type) stored in order.

##### Union Types

A union type describes a set of object types existing in the same overlapping
memory space.

#### Library Defined Types

The C standard library, and most system libraries, provide for a number of
types in addition to those specified by the language standard.  These tyeps are
largely `typedef`'s of basic integer types.  Below we will give a brief
description of some of the common data types that are defined in the standard
library and are supported by the FFI.

* `size_t` is defined by the C standard library to be an implementation defined
  unsigned integer type.  On our target platform it is 8 bytes.
* `clock_t` is an implementation dependent unsigned integer type representing
  CPu time.  On our target platform it is 8 bytes.
* `time_t` is an implementation dependent unsigned integer type representing
  real time.  On our target platform it is 8 bytes.
* `FILE` is a structure used for open files.
* `jmp_buf` is a structure used in combination with `longjmp` and related
  functions for saving and restoring program state.

#### Qualified and Unqualified Types

Types may be qualified with any combination of the qualifiers:

* `const` - meaning that the data of a variable is immutable
* `volatile` - meaning that any access of the variable is a side effect
* `restrict` - meaning that no other pointer aliases the qualified pointer

#### Casting, Conversions, and Compatibility

Certain types are considered compatible.  Arithmetic types are considered
compatible under the rules of *promotion* and *conversion*.  While the specific
details of promotion and conversion are too detailed to go into here, general
guidelines are given below:

* Integer or Floating Point promotions may promote a lower precision value to a
  higher precision
* An unsigned integer may only be promoted to a signed integer if the unsigned
  integer's entire range may be represented by the signed integer, otherwise
  both are promoted to a higher precision signed integer
* An integer may be promoted to a higher ranked real float without a loss in
  precision
* A floating point value converted to an integer value will have the fractional
  part of the value truncated (no rounding occurs) 
* Conversion of a complex value to a real value discards the imaginary
  component of the value, and the real portion of the value is converted as per
  floating point promotion rules.

### FFI Equivalents For Basic Types

The FFI provides type analogues for most C basic data (the `CTypes`) types in
`Foreign.C.Types`.  In addition to the basic arithmetic types the FFI provides
type analogues for many of the C standard library defined types.  The FFI
Standard does not require that the haskell implementation of the C type
analogues be identical in to the corresponding C types, but only that the
haskell types be able to hold a value *at least as large as* the C type on the
target system, and that the value of `Storable.sizeof (undefined :: CT)` is
equal to the expression `sizeof(CT)` for the corresponding `CType` in a C
application.  The FFI defines the set of typeclasses provided by the set of
`CTypes` that correspond to the C types, as shown below:

#### Important Typeclasses

There are several typeclasses either defined by the FFI or the haskell 98
standard that are used by the `CTypes`.

#### Integral Types

The FFI provides for `CTypes` corresponding to each of the integral types
provided by C, using a standard naming convention.  A selection is given below
and you may easily extrapolate the pattern.

##### Example Types

* `CChar`, `CUChar`, `CSChar`  -> `char`, `unsigned char`, `signed char`
* `CInt`, `CUInt` -> `int`, `unsigned int`
* `CULong`, `CLLong` -> `unsigned long`, `long long`

##### Typeclasses

The `CTypes` corresponding to the basic integral types provide:
`Eq`,`Ord`,`Num`,`Read`,`Show`,`Enum`,`Storable`,`Bounded`,`Real`,`Integral`,`Bits`

### Pointers

In C a pointer is an incomplete type that, in order to be complete, must point
to a specific type.  The haskell definition for the pointer type analagously
has kind `* -> *`.  The Haskell FFI defines two types of pointers:

* Data pointers are pointers to data.  There are three types of data pointers
  defined by the FFI:

    1. `Ptr a` 


* Function pointers are pointers to functions.

### The Storable Typeclass

### Opaque Types

### General Strategies

## <a id="functions">Functions</a>

## <a id="hsc">Using hsc2hs</a>

## <a id="guidelines">Guidelines</a>

## <a id="refs">References</a>

* [Haskell 98 Standard](http://www.haskell.org/onlinereport/)
* [Haskell 98 FFI Addendum](http://www.cse.unsw.edu.au/~chak/haskell/ffi/ffi/ffi.html).
* [Foreign.C.Types Documentation](http://www.haskell.org/ghc/docs/latest/html/libraries/base-4.6.0.1/Foreign-C-Types.html)
* [The ELF File Format](http://www.skyfree.org/linux/references/ELF_Format.pdf)
* [The AMD64 System V ABI](http://www.uclibc.org/docs/psABI-x86_64.pdf)
