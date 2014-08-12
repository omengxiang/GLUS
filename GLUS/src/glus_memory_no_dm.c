/*
 * GLUS - OpenGL 3 and 4 Utilities. Copyright (C) 2010 - 2014 Norbert Nopper
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "GL/glus.h"

// 16 MB of memory. Change only here, if more is needed.
// Division by 4 bytes, as uint32_t is used.
#define GLUS_MEMORY_SIZE (16*1024*1024/4)

// Number of memory table entries.
#define GLUS_MEMORY_TABLE_ENTRIES	1024

/**
 * Structure for the memory table entry.
 */
typedef struct _GLUSmemoryTable {

	/**
	 * Flag, if entry is valid.
	 */
	GLUSboolean valid;

	/**
	 * Flag, if entry is free.
	 */
	GLUSboolean free;

	/**
	 * Start index into the memory.
	 */
	size_t startIndex;

	/**
	 * Size of the allocated memory.
	 */
	size_t length;

	/**
	 * Pointer from the allocated memory.
	 */
	void* pointer;

} GLUSmemoryTable;

/**
 * Available memory with 4 byte alignment.
 */
static uint32_t g_memory[GLUS_MEMORY_SIZE];

/**
 * Memory table used to manage the memory array.
 */
static GLUSmemoryTable g_memoryTable[GLUS_MEMORY_TABLE_ENTRIES] = {{GLUS_TRUE, GLUS_TRUE, 0, GLUS_MEMORY_SIZE, (void*)g_memory}};

/**
 * Current amount of initialized memory table entries.
 */
static size_t g_memoryTableEntries = 1;

static GLUSboolean glusFindMemoryTableEntry(size_t* foundTableIndex)
{
	GLUSuint tableIndex = 0;

	if (!foundTableIndex)
	{
		return GLUS_FALSE;
	}

	while (tableIndex < g_memoryTableEntries && tableIndex < GLUS_MEMORY_TABLE_ENTRIES)
	{
		// If not valid, the table entry can be reused.
		if (!g_memoryTable[tableIndex].valid)
		{
			*foundTableIndex = tableIndex;

			return GLUS_TRUE;
		}

		tableIndex++;
	}

	return GLUS_FALSE;
}

static GLUSboolean glusInitMemoryTableEntry(size_t tableIndex, size_t startIndex, size_t length)
{
	if (tableIndex >= GLUS_MEMORY_TABLE_ENTRIES)
	{
		return GLUS_FALSE;
	}

	g_memoryTable[tableIndex].valid = GLUS_TRUE;
	g_memoryTable[tableIndex].free = GLUS_TRUE;
	g_memoryTable[tableIndex].startIndex = startIndex;
	g_memoryTable[tableIndex].length = length;

	g_memoryTable[tableIndex].pointer = (void*)g_memory[startIndex];

	if (tableIndex == g_memoryTableEntries)
	{
		g_memoryTableEntries++;
	}

	return GLUS_TRUE;
}

static GLUSvoid glusGarbageCollect()
{
	GLUSboolean continueGC = GLUS_TRUE;

	// Do garbage collection, until no memory has been merged.
	while(continueGC)
	{
		GLUSuint tableIndex = 0;

		continueGC = GLUS_FALSE;

		while (tableIndex < g_memoryTableEntries && tableIndex < GLUS_MEMORY_TABLE_ENTRIES)
		{
			if (g_memoryTable[tableIndex].valid && g_memoryTable[tableIndex].free)
			{
				GLUSuint otherTableIndex = 0;

				while (otherTableIndex < g_memoryTableEntries && otherTableIndex < GLUS_MEMORY_TABLE_ENTRIES)
				{
					if (otherTableIndex == tableIndex)
					{
						otherTableIndex++;

						continue;
					}

					if (g_memoryTable[otherTableIndex].valid && g_memoryTable[otherTableIndex].free)
					{
						// Check, if two entries are adjacent.
						if (g_memoryTable[tableIndex].startIndex + g_memoryTable[tableIndex].length / 4 == g_memoryTable[otherTableIndex].startIndex)
						{
							g_memoryTable[tableIndex].length += g_memoryTable[otherTableIndex].length;

							g_memoryTable[otherTableIndex].valid = GLUS_FALSE;

							continueGC = GLUS_TRUE;
						}
					}

					otherTableIndex++;
				}
			}

			tableIndex++;
		}
	}
}

static void* glusInternalMalloc(size_t size)
{
	GLUSuint tableIndex = 0;

	// Force 4 byte alignment.
	size_t allocatedLength = size % 4 == 0 ? size : (size / 4 + 1) * 4;

	if (allocatedLength < size)
	{
		return 0;
	}

	while (tableIndex < g_memoryTableEntries && tableIndex < GLUS_MEMORY_TABLE_ENTRIES)
	{
		// Search for a memory table entry, where the size fits in.
		if (g_memoryTable[tableIndex].valid && g_memoryTable[tableIndex].free && g_memoryTable[tableIndex].length >= size)
		{
			size_t otherTableIndex;

			// Try to reuse an entry.
			if (!glusFindMemoryTableEntry(&otherTableIndex))
			{
				otherTableIndex = tableIndex + 1;
			}

			// Assign the rest of the available memory to another table entry.
			if (!glusInitMemoryTableEntry(otherTableIndex, g_memoryTable[tableIndex].startIndex + allocatedLength / 4, g_memoryTable[tableIndex].length - allocatedLength))
			{
				// No empty entry could be found, so do not split and use all memory.
				allocatedLength = g_memoryTable[tableIndex].length;
			}

			// Entry now manages the requested memory.
			g_memoryTable[tableIndex].free = GLUS_FALSE;
			g_memoryTable[tableIndex].length = allocatedLength;

			return g_memoryTable[tableIndex].pointer;
		}

		tableIndex++;
	}

	return 0;
}

//

void* GLUSAPIENTRY glusMalloc(size_t size)
{
	void* pointer = 0;

	if (size == 0)
	{
		return pointer;
	}

	pointer = glusInternalMalloc(size);

	// If no memory was allocated ...
	if (!pointer)
	{
		// ... do garbage collection ...
		glusGarbageCollect();

		// ... and try to allocate again.
		pointer = glusInternalMalloc(size);
	}

	return pointer;
}

void GLUSAPIENTRY glusFree(void* pointer)
{
	GLUSuint tableIndex = 0;

	if (!pointer)
	{
		return;
	}

	// Search pointer ...
	while (tableIndex < g_memoryTableEntries && tableIndex < GLUS_MEMORY_TABLE_ENTRIES)
	{
		// ... and free memory by setting flag in table entry.
		if (g_memoryTable[tableIndex].pointer == pointer)
		{
			g_memoryTable[tableIndex].free = GLUS_TRUE;

			return;
		}

		tableIndex++;
	}
}
