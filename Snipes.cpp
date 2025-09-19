#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "config.h"
#include "Snipes.h"
#include "types.h"
#include "macros.h"
#include "console.h"
#include "timer.h"
#include "sound.h"
#include "keyboard.h"
#include "platform.h"
#ifdef __APPLE__
#include <SDL.h>
#endif

bool got_ctrl_break = false;
bool forfeit_match = false;
bool instant_quit = false;
bool sound_enabled = true;
bool shooting_sound_enabled = false;
BYTE fast_forward = false;
BYTE spacebar_state = false;
static BYTE keyboard_state = 0;
#ifdef CHEAT
bool rerecordingMode;
int single_step = 0;
int step_backwards = 0;
int increment_initial_seed = 0;
WORD skip_to_frame = 0;
#endif

#if defined(PLAYBACK_FOR_SCREEN_RECORDING) && !defined(CHEAT)
	#define _PLAYBACK_FOR_SCREEN_RECORDING 1
#else
	#define _PLAYBACK_FOR_SCREEN_RECORDING 0
#endif

static WORD random_seed_lo = 33, random_seed_hi = 467;
WORD GetRandomMasked(WORD mask)
{
	random_seed_lo *= 2;
	if (random_seed_lo  > 941)
		random_seed_lo -= 941;
	random_seed_hi *= 2;
	if (random_seed_hi  > 947)
		random_seed_hi -= 947;
	return (random_seed_lo + random_seed_hi) & mask;
}
template <WORD RANGE>
WORD GetRandomRanged()
{
	WORD mask = RANGE-1;
	mask |= mask >> 1;
	mask |= mask >> 2;
	mask |= mask >> 4;
	mask |= mask >> 8;
	if (mask == RANGE-1)
		return GetRandomMasked(mask);
	for (;;)
	{
		WORD number = GetRandomMasked(mask);
		if (number < RANGE)
			return number;
	}
}

Uint skillLevelLetter = 0;
Uint skillLevelNumber = 1;

void ParseSkillLevel(char *skillLevel, DWORD skillLevelLength)
{
	Uint skillLevelNumberTmp = 0;
	for (; skillLevelLength; skillLevel++, skillLevelLength--)
	{
		if (skillLevelNumberTmp >= 0x80)
		{
			// strange behavior, but this is what the original game does
			skillLevelNumber = 1;
			return;
		}
		char chr = *skillLevel;
		if (inrange(chr, 'a', 'z'))
			skillLevelLetter = chr - 'a';
		else
		if (inrange(chr, 'A', 'Z'))
			skillLevelLetter = chr - 'A';
		else
		if (inrange(chr, '0', '9'))
			skillLevelNumberTmp = skillLevelNumberTmp * 10 + (chr - '0');
	}
	if (inrange(skillLevelNumberTmp, 1, 9))
		skillLevelNumber = skillLevelNumberTmp;
}

void ReadSkillLevel()
{
	char skillLevel[0x80] = {};
	DWORD skillLevelLength = ReadTextFromConsole(skillLevel, _countof(skillLevel));

	if (skillLevelLength && skillLevel[skillLevelLength-1] == '\n')
		skillLevelLength--;
	if (skillLevelLength && skillLevel[skillLevelLength-1] == '\r')
		skillLevelLength--;

	for (Uint i=0; i<skillLevelLength; i++)
		if (skillLevel[i] != ' ')
		{
			ParseSkillLevel(skillLevel + i, skillLevelLength - i);
			break;
		}
}

static const BYTE snipeShootingAccuracyTable    ['Z'-'A'+1] = {2, 3, 4, 3, 4, 4, 3, 4, 3, 4, 4, 5, 3, 4, 3, 4, 3, 4, 3, 4, 4, 5, 4, 4, 5, 5}; // higher number = higher accuracy
static const bool enableSmallSnipesTable        ['Z'-'A'+1] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
static const bool bouncingBulletTable           ['Z'-'A'+1] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const BYTE smallSnipeExplosionChanceTable['Z'-'A'+1] = {0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x3F, 0x1F, 0x1F, 0x0F}; // lower number = higher chance
static const BYTE maxSnipesTable                ['9'-'1'+1] = { 10,  20,  30,  40,  60,  80, 100, 120, 150};
static const BYTE numSnipePortalsTable          ['9'-'1'+1] = {  3,   3,   4,   4,   5,   5,   6,   8,  10};
static const BYTE numLivesTable                 ['9'-'1'+1] = {  5,   5,   5,   5,   5,   4,   4,   3,   2};

bool enableElectricWalls, enableSmallSnipes, snipePortalsResistSnipeSpears, enableBouncingBullets;
BYTE snipeShootingAccuracy, smallSnipeExplosionChance, maxSnipes, numSnipePortalsAtStart, numLives;

enum MoveDirection : BYTE  // progresses from 0 to 7 in the clockwise direction
{
	MoveDirection_Up,
	MoveDirection_UpRight,
	MoveDirection_Right,
	MoveDirection_DownRight,
	MoveDirection_Down,
	MoveDirection_DownLeft,
	MoveDirection_Left,
	MoveDirection_UpLeft,
	MoveDirection_COUNT
};
enum MoveDirectionMask : BYTE
{
	MoveDirectionMask_Diagonal = 1,
	MoveDirectionMask_UpDown   = 4,
	MoveDirectionMask_All      = MoveDirection_COUNT - 1
};
MoveDirection  operator+ (MoveDirection  dir, int n                 ) {return (MoveDirection ) ((BYTE )dir +  n);   }
MoveDirection  operator- (MoveDirection  dir, int n                 ) {return (MoveDirection ) ((BYTE )dir -  n);   }
MoveDirection  operator& (MoveDirection  dir, MoveDirectionMask mask) {return (MoveDirection ) ((BYTE )dir &  mask);}
MoveDirection &operator+=(MoveDirection &dir, int n                 ) {return (MoveDirection&) ((BYTE&)dir += n);   }
MoveDirection &operator-=(MoveDirection &dir, int n                 ) {return (MoveDirection&) ((BYTE&)dir -= n);   }
MoveDirection &operator&=(MoveDirection &dir, MoveDirectionMask mask) {return (MoveDirection&) ((BYTE&)dir &= mask);}
MoveDirection &operator++(MoveDirection &dir                        ) {return (MoveDirection&)++(BYTE&)dir;         }
MoveDirection &operator--(MoveDirection &dir                        ) {return (MoveDirection&)--(BYTE&)dir;         }
MoveDirection  operator++(MoveDirection &dir, int                   ) {return (MoveDirection ) ((BYTE&)dir)++;      }
MoveDirection  operator--(MoveDirection &dir, int                   ) {return (MoveDirection ) ((BYTE&)dir)--;      }

enum OrthogonalDirection : BYTE
{
	OrthogonalDirection_Up,
	OrthogonalDirection_Right,
	OrthogonalDirection_Down,
	OrthogonalDirection_Left,
	OrthogonalDirection_COUNT
};
enum OrthogonalDirectionMask : BYTE
{
	OrthogonalDirectionMask_All = OrthogonalDirection_COUNT - 1
};
OrthogonalDirection  operator+ (OrthogonalDirection  dir, int n                       ) {return (OrthogonalDirection ) ((BYTE )dir +  n);   }
OrthogonalDirection  operator& (OrthogonalDirection  dir, OrthogonalDirectionMask mask) {return (OrthogonalDirection ) ((BYTE )dir &  mask);}
MoveDirection OrthoDirectionToMoveDirection(OrthogonalDirection dir) {return (MoveDirection)(dir << 1);}

static const BYTE bulletBounceTable[OrthogonalDirection_COUNT][MoveDirection_COUNT] =
{
	{4, 3, 4, 4, 4, 4, 4, 5,},
	{6, 7, 6, 5, 6, 6, 6, 6,},
	{0, 0, 0, 1, 0, 7, 0, 0,},
	{2, 2, 2, 2, 2, 3, 2, 1,},
};

struct Coord
{
	union
	{
		struct {BYTE x,y;};
		WORD xy;
	};
	Coord &operator=(WORD _xy) {xy = _xy; return *this;}
};

typedef BYTE ObjectIndex;

#ifdef OBJECT_TABLE_BINARY_COMPATIBILITY
typedef WORD sprite SpritePointer; // FAKE_POINTER to current sprite frame
#else
typedef const WORD *SpritePointer;
#endif

#define DEFINE_ANCESTOR(parent) operator parent&() {return *(parent*)this;}
#define DEFINE_OBJECT_AND_MEMBERS(className,member1,member2,member3,memberFunctions) \
struct className\
{\
	ObjectIndex next; /* objects[] index of the next object in the linked list of this object's type */\
	member1;\
	union\
	{\
		struct {BYTE x, y;};\
		Coord xy;\
	};\
	member2;\
	member3;\
	SpritePointer sprite;\
	memberFunctions\
};

DEFINE_OBJECT_AND_MEMBERS(Object,      BYTE unused1, BYTE unused2,    BYTE unused3,);
DEFINE_OBJECT_AND_MEMBERS(SnipePortal, BYTE unused,  BYTE spawnFrame, BYTE animFrame, DEFINE_ANCESTOR(Object));
DEFINE_OBJECT_AND_MEMBERS(Explosion,   BYTE unused,  BYTE spriteSize, BYTE animFrame, DEFINE_ANCESTOR(Object));

#define DEFINE_MOVING_OBJECT_AND_MEMBERS(className,member1,member2,memberFunctions) DEFINE_OBJECT_AND_MEMBERS(className, member1, MoveDirection moveDirection, member2, memberFunctions)

DEFINE_MOVING_OBJECT_AND_MEMBERS(MovingObject, BYTE unused1,       BYTE unused2,    DEFINE_ANCESTOR(Object));
DEFINE_MOVING_OBJECT_AND_MEMBERS(Player,       BYTE firingFrame,   BYTE inputFrame, DEFINE_ANCESTOR(Object) DEFINE_ANCESTOR(MovingObject));
DEFINE_MOVING_OBJECT_AND_MEMBERS(Enemy,        BYTE movementFlags, BYTE moveFrame,  DEFINE_ANCESTOR(Object) DEFINE_ANCESTOR(MovingObject));
DEFINE_MOVING_OBJECT_AND_MEMBERS(Weapon,       BYTE weaponType,    BYTE animFrame,  DEFINE_ANCESTOR(Object) DEFINE_ANCESTOR(MovingObject));

struct LargeSnipe : public Enemy { DEFINE_ANCESTOR(Object) DEFINE_ANCESTOR(MovingObject) };
struct SmallSnipe : public Enemy { DEFINE_ANCESTOR(Object) DEFINE_ANCESTOR(MovingObject) };

enum EnemyMovementFlag : BYTE
{
	EnemyMovementFlag_TurnDirection          = 1 << 0, // clear = clockwise, set = counterclockwise; set in stone when a snipe comes out of a snipe portal
	EnemyMovementFlag_SmallSnipeMoveStraight = 1 << 1, // for small snipes only; clear = move based on direction of player, set = keep moving in current direction until an obstacle is hit
};

enum SoundEffect : BYTE
{
	SoundEffect_Bullet            = 0,
	SoundEffect_Spear             = 1,
	SoundEffect_ExplodeSmallSnipe = 2,
	SoundEffect_ExplodeLargeSnipe = 3,
	SoundEffect_ExplodePlayer     = 4,
	SoundEffect_None = 0xFF
};

BYTE playerFiringPeriod; // in pairs of frames
WORD frame;
static bool playerAnimEyesNotWide, isPlayerDying, isPlayerExploding;
static BYTE lastHUD_numSmallSnipes, lastHUD_numSnipePortalsDestroyed, lastHUD_numLargeSnipes, lastHUD_numPlayerDeaths;
static BYTE numSnipePortals, numSmallSnipes, numWeapons, numPlayerDeaths, numLargeSnipes, playerEyeAnimFrame, orthoDistance;
static ObjectIndex objectHead_free, objectHead_weapons, objectHead_explosions, objectHead_smallSnipes, objectHead_largeSnipes, objectHead_snipePortals;
static OrthogonalDirection weaponCollisionDirection;
static SoundEffect currentSoundEffect = SoundEffect_None;
static BYTE currentSoundEffectFrame;
static WORD lastHUD_numSmallSnipesDestroyed, lastHUD_numLargeSnipesDestroyed, numSmallSnipesDestroyed, numLargeSnipesDestroyed, viewportFocusX, viewportFocusY;
static MazeTile *weaponTestPos;
static SHORT lastHUD_score, score;
Object *currentObject;
const WORD *currentSprite;

#define MAX_OBJECTS 0x100

static BYTE weaponBouncesRemaining[MAX_OBJECTS];

static Object objects[MAX_OBJECTS];

#define OBJECT_PLAYER          0x00
#define OBJECT_LASTFREE        0xFD
#define OBJECT_PLAYEREXPLOSION 0xFE

enum
{
	WeaponType_Bullet = 0, // fired by the player
	WeaponType_Spear  = 6, // fired by a snipe
};

template <typename OBJECT_TYPE>
OBJECT_TYPE &SetCurrentObject(ObjectIndex i)
{
	OBJECT_TYPE &object = ((OBJECT_TYPE*)objects)[i]; // cast the array before dereferencing, to avoid a bug that would otherwise happen with GCC's "-fstrict-aliasing" optimization
	currentObject = (Object*)&object;
	return object;
}

Player    &player          = ((Player   *)objects)[OBJECT_PLAYER         ];
Explosion &playerExplosion = ((Explosion*)objects)[OBJECT_PLAYEREXPLOSION];

static MazeTile maze[MAZE_WIDTH * MAZE_HEIGHT];

static const char statusLine[] = "\xDA\xBF\xB3\x01\x1A\xB3\x02\xB3""Skill""\xC0\xD9\xB3\x01\x1A\xB3\x02\xB3""Time  Men Left                 Score     0  0000001 Man Left""e";

void InitializeHUD()
{
	char (&scratchBuffer)[WINDOW_WIDTH] = (char(&)[WINDOW_WIDTH])maze;

	char skillLetter = skillLevelLetter + 'A';
	memset(scratchBuffer, ' ', WINDOW_WIDTH);
	outputText  (0x17, WINDOW_WIDTH, 0, 0, scratchBuffer);
	outputText  (0x17, WINDOW_WIDTH, 1, 0, scratchBuffer);
	outputText  (0x17, WINDOW_WIDTH, 2, 0, scratchBuffer);
	outputText  (0x17,     2, 0,  0, statusLine);
#ifdef CHEAT_OMNISCIENCE
	outputText  (0x17,     1, 0, 40, "\xB3");
	outputText  (0x17,     1, 1, 40, "\xB3");
	outputText  (0x17,     1, 2, 40, "\xB3");
#endif
	outputNumber(0x13,  0, 2, 0,  3, 0);
	outputText  (0x17,     1, 0,  6, statusLine+2);
	outputText  (0x13,     2, 0,  8, statusLine+3);
	outputNumber(0x13,  0, 4, 0, 11, 0);
	outputText  (0x17,     1, 0, 16, statusLine+5);
	outputText  (0x13,     1, 0, 18, statusLine+6);
	outputNumber(0x13,  0, 4, 0, 20, 0);
	outputText  (0x17,     1, 0, 25, statusLine+7);
	outputText  (0x17,     5, 0, 27, statusLine+8);
	outputNumber(0x17,  0, 1, 0, 38, skillLevelNumber);
	outputText  (0x17,     1, 0, 37, &skillLetter);
	outputText  (0x17,     2, 1,  0, statusLine+13);
	outputText  (0x17,     1, 1,  6, statusLine+15);
	outputText  (0x17,     2, 1,  8, statusLine+16);
	outputText  (0x17,     1, 1, 16, statusLine+18);
	outputText  (0x17,     1, 1, 18, statusLine+19);
	outputText  (0x17,     1, 1, 25, statusLine+20);
	outputText  (0x17,     4, 1, 27, statusLine+21);
	memcpy(scratchBuffer, statusLine+25, 40);
	scratchBuffer[0] = numLives + '0';
	scratchBuffer[1] = 0;
	if (numLives == 1)
		scratchBuffer[6] = 'a';
	outputText  (0x17, 40, 2,  0, scratchBuffer);

	lastHUD_numSmallSnipes = 0xFF;
	lastHUD_numSnipePortalsDestroyed = 0xFF;
	lastHUD_numLargeSnipes = 0xFF;
	lastHUD_numSmallSnipesDestroyed = 0xFFFF;
	lastHUD_numLargeSnipesDestroyed = 0xFFFF;
	lastHUD_numPlayerDeaths = 0xFF;
	lastHUD_score = -1;
}

#define MAZE_SCRATCH_BUFFER_SIZE (MAZE_WIDTH_IN_CELLS * MAZE_HEIGHT_IN_CELLS)

void CreateMaze_helper(SHORT &data_1E0, BYTE data_2AF)
{
	switch (data_2AF)
	{
	case 0:
		data_1E0 -= MAZE_WIDTH_IN_CELLS;
		if (data_1E0 < 0)
			data_1E0 += MAZE_SCRATCH_BUFFER_SIZE;
		break;
	case 1:
		data_1E0 += MAZE_WIDTH_IN_CELLS;
		if (data_1E0 >= MAZE_SCRATCH_BUFFER_SIZE)
			data_1E0 -= MAZE_SCRATCH_BUFFER_SIZE;
		break;
	case 2:
		if ((data_1E0 & 0xF) == 0)
			data_1E0 += 0xF;
		else
			data_1E0--;
		break;
	case 3:
		if ((data_1E0 & 0xF) == 0xF)
			data_1E0 -= 0xF;
		else
			data_1E0++;
		break;
	default:
		UNREACHABLE;
	}
}

void CreateMaze()
{
	BYTE (&mazeScratchBuffer)[MAZE_SCRATCH_BUFFER_SIZE] = (BYTE(&)[MAZE_SCRATCH_BUFFER_SIZE])objects;

	memset(mazeScratchBuffer, 0xF, MAZE_SCRATCH_BUFFER_SIZE);
	mazeScratchBuffer[0] = 0xE;
	mazeScratchBuffer[1] = 0xD;
	Uint numMazeCellsUninitialized = MAZE_SCRATCH_BUFFER_SIZE - 2;

	static const BYTE data_EA3[] = {8, 4, 2, 1};
	static const BYTE data_EA7[] = {4, 8, 1, 2};

	for (;;)
	{
	outer_loop:
		if (!numMazeCellsUninitialized)
			break;
		WORD data_1DE = GetRandomRanged<MAZE_SCRATCH_BUFFER_SIZE>();
		if (mazeScratchBuffer[data_1DE] != 0xF)
			continue;
		WORD data_1E0;
		WORD data_1E2 = GetRandomMasked(3);
		WORD data_1E4 = 0;
		BYTE data_2AF;
		for (;;)
		{
			if (data_1E4 > 3)
				goto outer_loop;
			data_2AF = data_1E2 & 3;
			data_1E0 = data_1DE;
			CreateMaze_helper((SHORT&)data_1E0, data_2AF);
			if (mazeScratchBuffer[data_1E0] != 0xF)
				break;
			data_1E2++;
			data_1E4++;
		}
		numMazeCellsUninitialized--;
		mazeScratchBuffer[data_1E0] ^= data_EA7[data_2AF];
		mazeScratchBuffer[data_1DE] ^= data_EA3[data_2AF];
		data_1E2 = data_1DE;
		for (;;)
		{
			BYTE data_2AF = (BYTE)GetRandomMasked(3);
			BYTE data_2B0 = (BYTE)GetRandomMasked(3) + 1;
			data_1E0 = data_1E2;
			for (;;)
			{
				CreateMaze_helper((SHORT&)data_1E0, data_2AF);
				if (!data_2B0 || mazeScratchBuffer[data_1E0] != 0xF)
					break;
				mazeScratchBuffer[data_1E0] ^= data_EA7[data_2AF];
				mazeScratchBuffer[data_1E2] ^= data_EA3[data_2AF];
				numMazeCellsUninitialized--;
				data_2B0--;
				data_1E2 = data_1E0;
			}
			if (data_2B0)
				goto outer_loop;
		}
	}
	for (WORD data_1DE = 0; data_1DE < 0x40; data_1DE++)
	{
		WORD data_1E0 = GetRandomRanged<MAZE_SCRATCH_BUFFER_SIZE>();
		BYTE data_2AF = (BYTE)GetRandomMasked(3);
		mazeScratchBuffer[data_1E0] &= ~data_EA3[data_2AF];
		CreateMaze_helper((SHORT&)data_1E0, data_2AF);
		mazeScratchBuffer[data_1E0] &= ~data_EA7[data_2AF];
	}
	for (Uint i=0; i<_countof(maze); i++)
		maze[i] = MazeTile(0x9, ' ');
	WORD data_1E4 = 0;
	WORD data_1E2 = 0;
	for (WORD data_1DE = 0; data_1DE < MAZE_HEIGHT_IN_CELLS; data_1DE++)
	{
		for (WORD data_1E0 = 0; data_1E0 < MAZE_WIDTH_IN_CELLS; data_1E0++)
		{
			if (mazeScratchBuffer[data_1E2] & 8)
				for (Uint i=0; i<MAZE_CELL_WIDTH-1; i++)
					maze[data_1E4 + 1 + i] = MazeTile(0x9, 0xCD);
			if (mazeScratchBuffer[data_1E2] & 2)
			{
				WORD data_1E6 = data_1E4 + MAZE_WIDTH;
				for (WORD data_1E8 = 1; data_1E8 <= MAZE_CELL_HEIGHT-1; data_1E8++)
				{
					maze[data_1E6] = MazeTile(0x9, 0xBA);
					data_1E6 += MAZE_WIDTH;
				}
			}
			WORD data_1E6;
			if (data_1DE)
				data_1E6 = data_1E2 - (!data_1E0 ? 1 : 1 + MAZE_WIDTH_IN_CELLS);
			else
			{
				if (!data_1E0)
					data_1E6 = MAZE_SCRATCH_BUFFER_SIZE - 1;
				else
					data_1E6 = data_1E2 + MAZE_SCRATCH_BUFFER_SIZE - (1 + MAZE_WIDTH_IN_CELLS);
			}
			static BYTE data_EAB[] = {0x20, 0xBA, 0xBA, 0xBA, 0xCD, 0xBC, 0xBB, 0xB9, 0xCD, 0xC8, 0xC9, 0xCC, 0xCD, 0xCA, 0xCB, 0xCE};
			maze[data_1E4].chr = data_EAB[(mazeScratchBuffer[data_1E2] & 0xA) | (mazeScratchBuffer[data_1E6] & 0x5)];
			maze[data_1E4].color = 0x9;
			data_1E4 += MAZE_CELL_WIDTH;
			data_1E2++;
		}
		data_1E4 += MAZE_WIDTH * (MAZE_CELL_HEIGHT-1);
	}
}

BYTE CreateNewObject()
{
	ObjectIndex object = objectHead_free;
	if (object)
		objectHead_free = objects[object].next;
	return object;
}

#define EXPLOSION_SIZE(x,y) ((y)*10+(x))

#define SPRITE_SIZE(x,y) (((x)<<8)+(y))

// snipe portal
static const WORD data_1002[] = {SPRITE_SIZE(2,2), 0xFDA, 0xEBF, 0xDC0, 0xCD9};
static const WORD data_100C[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD data_1016[] = {SPRITE_SIZE(2,2), 0xEDA, 0xEBF, 0xEC0, 0xED9};
static const WORD data_1020[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD data_102A[] = {SPRITE_SIZE(2,2), 0xDDA, 0xDBF, 0xDC0, 0xDD9};
static const WORD data_1034[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD data_103E[] = {SPRITE_SIZE(2,2), 0xCDA, 0xCBF, 0xCC0, 0xCD9};
static const WORD data_1048[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD data_1052[] = {SPRITE_SIZE(2,2), 0xBDA, 0xBBF, 0xBC0, 0xBD9};
static const WORD data_105C[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD data_1066[] = {SPRITE_SIZE(2,2), 0xADA, 0xABF, 0xAC0, 0xAD9};
static const WORD data_1070[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD data_107A[] = {SPRITE_SIZE(2,2), 0x9DA, 0x9BF, 0x9C0, 0x9D9};
static const WORD data_1084[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD data_108E[] = {SPRITE_SIZE(2,2), 0x4DA, 0x4BF, 0x4C0, 0x4D9};
static const WORD data_1098[] = {SPRITE_SIZE(2,2), 0xEFF, 0xEFF, 0xEFF, 0xEFF};
static const WORD *data_10A2[] = {data_1002, data_100C, data_1016, data_1020, data_102A, data_1034, data_103E, data_1048, data_1052, data_105C, data_1066, data_1070, data_107A, data_1084, data_108E, data_1098};
// player
static const WORD data_10E2[] = {SPRITE_SIZE(2,2), 0xF93, 0xF93, 0xF11, 0xF10};
static const WORD data_10EC[] = {SPRITE_SIZE(2,2), 0xF4F, 0xF4F, 0xF11, 0xF10};
//static const WORD *data_10F6[] = {data_10E2, data_10EC};
// small snipe
static const WORD data_10FE[] = {SPRITE_SIZE(1,1), 0x202};
// large snipe
static const WORD data_1108[] = {SPRITE_SIZE(2,1), 0x201, 0x218};
static const WORD data_1112[] = {SPRITE_SIZE(2,1), 0x201, 0x21A};
static const WORD data_111C[] = {SPRITE_SIZE(2,1), 0x201, 0x219};
static const WORD data_1126[] = {SPRITE_SIZE(2,1), 0x21B, 0x201};
static const WORD *data_1130[] = {data_1108, data_1112, data_1112, data_1112, data_111C, data_1126, data_1126, data_1126};
// bullet
static const WORD data_1150[] = {SPRITE_SIZE(1,1), 0xE09};
static const WORD data_115A[] = {SPRITE_SIZE(1,1), 0xB0F};
static const WORD *data_11D4[] = {data_1150, data_115A, data_1150, data_115A};
// spear
static const WORD data_1164[] = {SPRITE_SIZE(1,1), 0xA18};
static const WORD data_116E[] = {SPRITE_SIZE(1,1), 0xA2F};
static const WORD data_1178[] = {SPRITE_SIZE(1,1), 0xA1A};
static const WORD data_1182[] = {SPRITE_SIZE(1,1), 0xA5C};
static const WORD data_118C[] = {SPRITE_SIZE(1,1), 0xA19};
static const WORD data_1196[] = {SPRITE_SIZE(1,1), 0xA2F};
static const WORD data_11A0[] = {SPRITE_SIZE(1,1), 0xA1B};
static const WORD data_11AA[] = {SPRITE_SIZE(1,1), 0xA5C};
static const WORD *data_11B4[] = {data_1164, data_116E, data_1178, data_1182, data_118C, data_1196, data_11A0, data_11AA};
// player or snipe portal explosion
static const WORD data_12C2[] = {SPRITE_SIZE(2,2), 0xFB0, 0xFB2, 0xFB2, 0xFB0};
static const WORD data_12CC[] = {SPRITE_SIZE(2,2), 0xBB2, 0xBB0, 0xBB0, 0xBB2};
static const WORD data_12D6[] = {SPRITE_SIZE(2,2), 0xCB0, 0xCB2, 0xCB2, 0xCB0};
static const WORD data_12E0[] = {SPRITE_SIZE(2,2), 0x4B2, 0x4B0, 0x4B0, 0x4B2};
static const WORD data_12EA[] = {SPRITE_SIZE(2,2), 0x62A, 0x60F, 0x62A, 0x60F};
static const WORD data_12F4[] = {SPRITE_SIZE(2,2), 0x807, 0x820, 0x807, 0x820};
static const WORD *data_12FE[] = {data_12C2, data_12CC, data_12D6, data_12E0, data_12EA, data_12F4};
// large snipe explosion
static const WORD data_1316[] = {SPRITE_SIZE(2,1), 0xFB0, 0xFB2};
static const WORD data_1320[] = {SPRITE_SIZE(2,1), 0xBB2, 0xBB0};
static const WORD data_132A[] = {SPRITE_SIZE(2,1), 0xCB0, 0xCB2};
static const WORD data_1334[] = {SPRITE_SIZE(2,1), 0x4B2, 0x4B0};
static const WORD data_133E[] = {SPRITE_SIZE(2,1), 0x62A, 0x60F};
static const WORD data_1348[] = {SPRITE_SIZE(2,1), 0x807, 0x820};
static const WORD *data_1352[] = {data_1316, data_1320, data_132A, data_1334, data_133E, data_1348};
// small snipe explosion
static const WORD data_136A[] = {SPRITE_SIZE(1,1), 0xFB2};
static const WORD data_1374[] = {SPRITE_SIZE(1,1), 0xB0F};
static const WORD data_137E[] = {SPRITE_SIZE(1,1), 0xC09};
static const WORD data_1388[] = {SPRITE_SIZE(1,1), 0x407};
static const WORD *data_1392[] = {data_136A, data_136A, data_136A, data_1374, data_137E, data_1388};

bool IsPlayer(BYTE chr)
{
	return chr == 0x93 || chr == 0x4F || chr == 0x11 || chr == 0x10;
}
bool IsSnipePortal(MazeTile tile)
{
#if defined(FIX_BUGS) || defined(FIX_SNIPE_PORTAL_IDENTIFICATION_BUG)
	return tile.chr == 0xDA || tile.chr == 0xBF || tile.chr == 0xC0 || tile.chr == 0xD9 || tile.chr == 0xFF;
#else
	for (Uint i=1; i<_countof(data_1002); i++)
		if ((WORD&)tile == data_1002[i])
			return true;
	return tile.chr == 0xFF;
#endif
}

#ifdef OBJECT_TABLE_BINARY_COMPATIBILITY

#define FAKE_POINTER(n) 0x##n

const WORD *FakePointerToPointer(WORD fakePtr)
{
	switch (fakePtr)
	{
	case FAKE_POINTER(1002): return data_1002;
	case FAKE_POINTER(100C): return data_100C;
	case FAKE_POINTER(1016): return data_1016;
	case FAKE_POINTER(1020): return data_1020;
	case FAKE_POINTER(102A): return data_102A;
	case FAKE_POINTER(1034): return data_1034;
	case FAKE_POINTER(103E): return data_103E;
	case FAKE_POINTER(1048): return data_1048;
	case FAKE_POINTER(1052): return data_1052;
	case FAKE_POINTER(105C): return data_105C;
	case FAKE_POINTER(1066): return data_1066;
	case FAKE_POINTER(1070): return data_1070;
	case FAKE_POINTER(107A): return data_107A;
	case FAKE_POINTER(1084): return data_1084;
	case FAKE_POINTER(108E): return data_108E;
	case FAKE_POINTER(1098): return data_1098;
	case FAKE_POINTER(10E2): return data_10E2;
	case FAKE_POINTER(10EC): return data_10EC;
	case FAKE_POINTER(10FE): return data_10FE;
	case FAKE_POINTER(1108): return data_1108;
	case FAKE_POINTER(1112): return data_1112;
	case FAKE_POINTER(111C): return data_111C;
	case FAKE_POINTER(1126): return data_1126;
	case FAKE_POINTER(1150): return data_1150;
	case FAKE_POINTER(115A): return data_115A;
	case FAKE_POINTER(1164): return data_1164;
	case FAKE_POINTER(116E): return data_116E;
	case FAKE_POINTER(1178): return data_1178;
	case FAKE_POINTER(1182): return data_1182;
	case FAKE_POINTER(118C): return data_118C;
	case FAKE_POINTER(1196): return data_1196;
	case FAKE_POINTER(11A0): return data_11A0;
	case FAKE_POINTER(11AA): return data_11AA;
	case FAKE_POINTER(12C2): return data_12C2;
	case FAKE_POINTER(12CC): return data_12CC;
	case FAKE_POINTER(12D6): return data_12D6;
	case FAKE_POINTER(12E0): return data_12E0;
	case FAKE_POINTER(12EA): return data_12EA;
	case FAKE_POINTER(12F4): return data_12F4;
	case FAKE_POINTER(1316): return data_1316;
	case FAKE_POINTER(1320): return data_1320;
	case FAKE_POINTER(132A): return data_132A;
	case FAKE_POINTER(1334): return data_1334;
	case FAKE_POINTER(133E): return data_133E;
	case FAKE_POINTER(1348): return data_1348;
	case FAKE_POINTER(136A): return data_136A;
	case FAKE_POINTER(1374): return data_1374;
	case FAKE_POINTER(137E): return data_137E;
	case FAKE_POINTER(1388): return data_1388;
	default:
		return NULL;
	}
}

WORD PointerToFakePointer(const WORD *ptr)
{
	if (ptr == data_1002) return FAKE_POINTER(1002);
	if (ptr == data_100C) return FAKE_POINTER(100C);
	if (ptr == data_1016) return FAKE_POINTER(1016);
	if (ptr == data_1020) return FAKE_POINTER(1020);
	if (ptr == data_102A) return FAKE_POINTER(102A);
	if (ptr == data_1034) return FAKE_POINTER(1034);
	if (ptr == data_103E) return FAKE_POINTER(103E);
	if (ptr == data_1048) return FAKE_POINTER(1048);
	if (ptr == data_1052) return FAKE_POINTER(1052);
	if (ptr == data_105C) return FAKE_POINTER(105C);
	if (ptr == data_1066) return FAKE_POINTER(1066);
	if (ptr == data_1070) return FAKE_POINTER(1070);
	if (ptr == data_107A) return FAKE_POINTER(107A);
	if (ptr == data_1084) return FAKE_POINTER(1084);
	if (ptr == data_108E) return FAKE_POINTER(108E);
	if (ptr == data_1098) return FAKE_POINTER(1098);
	if (ptr == data_10E2) return FAKE_POINTER(10E2);
	if (ptr == data_10EC) return FAKE_POINTER(10EC);
	if (ptr == data_10FE) return FAKE_POINTER(10FE);
	if (ptr == data_1108) return FAKE_POINTER(1108);
	if (ptr == data_1112) return FAKE_POINTER(1112);
	if (ptr == data_111C) return FAKE_POINTER(111C);
	if (ptr == data_1126) return FAKE_POINTER(1126);
	if (ptr == data_1150) return FAKE_POINTER(1150);
	if (ptr == data_115A) return FAKE_POINTER(115A);
	if (ptr == data_1164) return FAKE_POINTER(1164);
	if (ptr == data_116E) return FAKE_POINTER(116E);
	if (ptr == data_1178) return FAKE_POINTER(1178);
	if (ptr == data_1182) return FAKE_POINTER(1182);
	if (ptr == data_118C) return FAKE_POINTER(118C);
	if (ptr == data_1196) return FAKE_POINTER(1196);
	if (ptr == data_11A0) return FAKE_POINTER(11A0);
	if (ptr == data_11AA) return FAKE_POINTER(11AA);
	if (ptr == data_12C2) return FAKE_POINTER(12C2);
	if (ptr == data_12CC) return FAKE_POINTER(12CC);
	if (ptr == data_12D6) return FAKE_POINTER(12D6);
	if (ptr == data_12E0) return FAKE_POINTER(12E0);
	if (ptr == data_12EA) return FAKE_POINTER(12EA);
	if (ptr == data_12F4) return FAKE_POINTER(12F4);
	if (ptr == data_1316) return FAKE_POINTER(1316);
	if (ptr == data_1320) return FAKE_POINTER(1320);
	if (ptr == data_132A) return FAKE_POINTER(132A);
	if (ptr == data_1334) return FAKE_POINTER(1334);
	if (ptr == data_133E) return FAKE_POINTER(133E);
	if (ptr == data_1348) return FAKE_POINTER(1348);
	if (ptr == data_136A) return FAKE_POINTER(136A);
	if (ptr == data_1374) return FAKE_POINTER(1374);
	if (ptr == data_137E) return FAKE_POINTER(137E);
	if (ptr == data_1388) return FAKE_POINTER(1388);
	__debugbreak();
	return 0;
}

#else // OBJECT_TABLE_BINARY_COMPATIBILITY

#define FAKE_POINTER(n) (data_##n)
#define FakePointerToPointer(n) (n)
#define PointerToFakePointer(n) (n)

#endif // OBJECT_TABLE_BINARY_COMPATIBILITY

#ifdef USE_MODULO_LOOKUP_TABLE
static const bool data_11E8[] = {
	0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,
	0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,
	0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0
};

bool IsDiagonalDoubledPhase(BYTE n)
{
	return data_11E8[n];
}
#else
bool IsDiagonalDoubledPhase(BYTE n)
{
	return n % 3 & 1;
}
#endif

static const BYTE mazeWallCharacters[] = {0xB9, 0xBA, 0xBB, 0xBC, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE};
static const BYTE    enemyCharacters[] = {0x01, 0x02, 0x18, 0x1A, 0x19, 0x1B};

void FreeObject(ObjectIndex object)
{
	objects[object].next = objectHead_free;
	objectHead_free = object;
}

bool IsObjectLocationOccupied(BYTE y, BYTE x)
{
	BYTE spriteHeight = ((BYTE*)currentSprite)[0];
	BYTE spriteWidth  = ((BYTE*)currentSprite)[1];
	MazeTile *mazeTile = &maze[y * MAZE_WIDTH];
	for (BYTE row = 0; row < spriteHeight; row++)
	{
		for (BYTE column = 0; column < spriteWidth; column++)
		{
			if (mazeTile[x].chr != ' ')
				return true;
			if (++x >= MAZE_WIDTH)
				x = 0;
		}
		mazeTile += MAZE_WIDTH;
		if (mazeTile >= &maze[_countof(maze)])
			mazeTile -=       _countof(maze);
	}
	return false;
}

void PlotObjectToMaze() // plots object currentObject with sprite currentSprite
{
	BYTE spriteHeight = ((BYTE*)currentSprite)[0];
	BYTE spriteWidth  = ((BYTE*)currentSprite)[1];
	MazeTile *spriteTile = (MazeTile*)&currentSprite[1];
	MazeTile *mazeTile = &maze[currentObject->y * MAZE_WIDTH];
	for (BYTE row = 0; row < spriteHeight; row++)
	{
		BYTE x = currentObject->x;
		for (BYTE column = 0; column < spriteWidth; column++)
		{
			mazeTile[x] = *spriteTile++;
			if (++x >= MAZE_WIDTH)
				x = 0;
		}
		mazeTile += MAZE_WIDTH;
		if (mazeTile >= &maze[_countof(maze)])
			mazeTile -=       _countof(maze);
	}
}

void PlaceObjectInRandomUnoccupiedMazeCell()
{
	do
	{
		currentObject->x = (BYTE)GetRandomRanged<MAZE_WIDTH  / MAZE_CELL_WIDTH >() * MAZE_CELL_WIDTH  + MAZE_CELL_WIDTH /2;
		currentObject->y = (BYTE)GetRandomRanged<MAZE_HEIGHT / MAZE_CELL_HEIGHT>() * MAZE_CELL_HEIGHT + MAZE_CELL_HEIGHT/2;
	}
	while (IsObjectLocationOccupied(currentObject->y, currentObject->x));
}

void CreateSnipePortalsAndPlayer()
{
	for (WORD data_B58 = 1; data_B58 <= OBJECT_LASTFREE; data_B58++)
		objects[data_B58].next = data_B58 + 1;
	objects[OBJECT_LASTFREE].next = 0;
	objectHead_free = 1;
	objectHead_weapons = 0;
	objectHead_explosions = 0;
	objectHead_smallSnipes = 0;
	objectHead_largeSnipes = 0;
	objectHead_snipePortals = 0;
	for (WORD data_B58 = 0; data_B58 < numSnipePortalsAtStart; data_B58++)
	{
		ObjectIndex newSnipePortal = CreateNewObject();
		objects[newSnipePortal].next = objectHead_snipePortals;
		objectHead_snipePortals = newSnipePortal;
		SnipePortal &snipePortal = SetCurrentObject<SnipePortal>(newSnipePortal);
		snipePortal.sprite = FAKE_POINTER(1002);
		currentSprite = data_1002;
		PlaceObjectInRandomUnoccupiedMazeCell();
		snipePortal.unused = 0;
		snipePortal.animFrame = (BYTE)GetRandomMasked(0xF);
		snipePortal.spawnFrame = 1;
		PlotObjectToMaze();
	}
	numSnipePortals = numSnipePortalsAtStart;
	numSmallSnipesDestroyed = 0;
	numSmallSnipes = 0;
	numWeapons = 0;
	numLargeSnipesDestroyed = 0;
	numPlayerDeaths = 0;
	numLargeSnipes = 0;
	score = 0;
	playerAnimEyesNotWide = false;
	playerEyeAnimFrame = 0;
	isPlayerDying = false;
	isPlayerExploding = false;
	player.sprite = FAKE_POINTER(10E2);
	currentSprite = data_10E2;
	currentObject = (Object*)&player;
	PlaceObjectInRandomUnoccupiedMazeCell();
	PlotObjectToMaze();
	viewportFocusX = player.x;
	viewportFocusY = player.y;
	player.inputFrame = 1;
	player.firingFrame = 1;
}

void SetSoundEffectState(BYTE frame, SoundEffect index)
{
	if (currentSoundEffect != SoundEffect_None && index < currentSoundEffect)
		return;
	if (!shooting_sound_enabled && !index)
		return;
	currentSoundEffectFrame = frame;
	currentSoundEffect      = index;
}

void DrawViewport();

bool UpdateHUD(bool incrementFrame = true) // returns true if the match has been won
{
	frame += incrementFrame;
	if (lastHUD_numLargeSnipesDestroyed != numLargeSnipesDestroyed)
		outputNumber(0x13, 0, 4, 0, 11, lastHUD_numLargeSnipesDestroyed = numLargeSnipesDestroyed);
	if (lastHUD_numSmallSnipesDestroyed != numSmallSnipesDestroyed)
		outputNumber(0x13, 0, 4, 0, 20, lastHUD_numSmallSnipesDestroyed = numSmallSnipesDestroyed);
	if (lastHUD_numSnipePortalsDestroyed != numSnipePortals)
	{
		outputNumber(0x17, 0, 2, 1,  3, lastHUD_numSnipePortalsDestroyed = numSnipePortals);
		outputNumber(0x13, 0, 2, 0,  3, numSnipePortalsAtStart - numSnipePortals);
	}
	if (lastHUD_numLargeSnipes != numLargeSnipes)
		outputNumber(0x17, 0, 3, 1, 12, lastHUD_numLargeSnipes = numLargeSnipes);
	if (lastHUD_numSmallSnipes != numSmallSnipes)
		outputNumber(0x17, 0, 3, 1, 21, lastHUD_numSmallSnipes = numSmallSnipes);
	if (lastHUD_score != score)
	{
		lastHUD_score = score;
		if (lastHUD_score > 0)
			outputNumber(0x17, 1, 5, 2, 33, lastHUD_score);
		else
			outputText  (0x17,    6, 2, 33, statusLine+65);
	}
#ifdef CHEAT_OMNISCIENCE
	{
		char hex[strlength("RNG 941,947")+1];
		sprintf(hex, "RNG %03u,%03u", random_seed_lo, random_seed_hi);
		outputText(0x17, strlength("RNG 941,947"), 2, 42, hex);
	}
#endif
	if (lastHUD_numPlayerDeaths != numPlayerDeaths)
	{
		BYTE livesRemaining = numLives - (lastHUD_numPlayerDeaths = numPlayerDeaths);
		if (livesRemaining == 1)
			outputText  (0x1C,   10, 2,  0, statusLine+71);
		else
		{
			{}	outputNumber(0x17, 0, 1, 2,  0, livesRemaining);
			if (!livesRemaining)
			{
				outputNumber(0x1C, 0, 1, 2,  0, 0);
				outputText  (0x1C,    1, 2,  3, statusLine+81);
			}
		}
	}
	outputNumber(0x17, 0, 5, 1, 34, frame); // Time

	if (numLargeSnipes || numSnipePortals || numSmallSnipes)
		return false;

	DrawViewport();
	EraseBottomTwoLines();
	CloseDirectConsole(WINDOW_HEIGHT-2);
	WriteTextToConsole("Congratulations --- YOU ACTUALLY WON!!!\r\n");
	return true;
}

void ExplodeObject(ObjectIndex arg)
{
	if (arg == OBJECT_PLAYER) // explode the player (and don't overwrite the player object)
	{
		arg = OBJECT_PLAYEREXPLOSION;
		playerExplosion.x = player.x;
		playerExplosion.y = player.y;
		playerExplosion.spriteSize = EXPLOSION_SIZE(2,2);
		playerExplosion.sprite = FAKE_POINTER(12C2);
		isPlayerExploding = true;
	}
	Explosion &explosion = SetCurrentObject<Explosion>(arg);
	explosion.next = objectHead_explosions;
	objectHead_explosions = arg;
	const WORD *data_CC4 = FakePointerToPointer(explosion.sprite);
	BYTE data_CC8 = ((BYTE*)data_CC4)[0];
	BYTE data_CC9 = ((BYTE*)data_CC4)[1];
	if (data_CC8 == 2 && data_CC9 == 2)
	{
		explosion.sprite = FAKE_POINTER(12C2);
		currentSprite = data_12C2;
		explosion.spriteSize = EXPLOSION_SIZE(2,2);
		explosion.animFrame = 0;
		SetSoundEffectState(0, SoundEffect_ExplodePlayer);
	}
	if (data_CC8 == 1 && data_CC9 == 2)
	{
		explosion.sprite = FAKE_POINTER(1316);
		currentSprite = data_1316;
		explosion.spriteSize = EXPLOSION_SIZE(2,1);
		explosion.animFrame = 0;
		SetSoundEffectState(0, SoundEffect_ExplodeLargeSnipe);
	}
	if (data_CC8 == 1 && data_CC9 == 1)
	{
		explosion.sprite = FAKE_POINTER(136A);
		currentSprite = data_136A;
		explosion.spriteSize = EXPLOSION_SIZE(1,1);
		explosion.animFrame = 2;
		SetSoundEffectState(2, SoundEffect_ExplodeSmallSnipe);
	}
	PlotObjectToMaze();
}

void FreeObjectInList_worker(ObjectIndex *objectHead, ObjectIndex object)
{
	ObjectIndex data_CCA = *objectHead;
	if (object == data_CCA)
	{
		*objectHead = objects[object].next;
		return;
	}
	for (;;)
	{
		ObjectIndex data_CCB = objects[data_CCA].next;
		if (!data_CCB)
			return;
		if (object == data_CCB)
			break;
		data_CCA = data_CCB;
	}
	objects[data_CCA].next = objects[object].next;
}

void FreeObjectInList(ObjectIndex *objectHead, ObjectIndex object)
{
	if (object == OBJECT_PLAYER)
	{
		ExplodeObject(object);
		return;
	}
	FreeObjectInList_worker(objectHead, object);
	if (objectHead != &objectHead_weapons)
	{
		ExplodeObject(object);
		return;
	}
	FreeObject(object);
}

bool MoveWeaponAndTestHit(OrthogonalDirection arg)
{
	switch (weaponCollisionDirection = arg)
	{
	case OrthogonalDirection_Up:
		weaponTestPos -= MAZE_WIDTH;
		if (--currentObject->y == 0xFF)
		{
			currentObject->y = MAZE_HEIGHT - 1;
			weaponTestPos += _countof(maze);
		}
		break;
	case OrthogonalDirection_Right:
		weaponTestPos++;
		if (++currentObject->x >= MAZE_WIDTH)
		{
			currentObject->x = 0;
			weaponTestPos -= MAZE_WIDTH;
		}
		break;
	case OrthogonalDirection_Down:
		weaponTestPos += MAZE_WIDTH;
		if (++currentObject->y >= MAZE_HEIGHT)
		{
			currentObject->y = 0;
			weaponTestPos -= _countof(maze);
		}
		break;
	case OrthogonalDirection_Left:
		weaponTestPos--;
		if (--currentObject->x == 0xFF)
		{
			currentObject->x = MAZE_WIDTH - 1;
			weaponTestPos += MAZE_WIDTH;
		}
		break;
	default:
		UNREACHABLE;
	}
	return weaponTestPos->chr != ' ';
}

void UpdateWeapons()
{
	ObjectIndex prevObject = 0;
	for (ObjectIndex object = objectHead_weapons; object;)
	{
		Weapon &weapon = SetCurrentObject<Weapon>(object);
		weaponTestPos = &maze[weapon.y * MAZE_WIDTH + weapon.x];
		if (weaponTestPos->chr == 0xB2)
		{
			ObjectIndex nextObject = weapon.next;
			*weaponTestPos = MazeTile(0x9, ' ');
			FreeObjectInList(&objectHead_weapons, object);
			numWeapons--;
			object = nextObject;
			continue;
		}
		*weaponTestPos = MazeTile(0x9, ' ');
		switch (weapon.moveDirection)
		{
		case MoveDirection_UpRight:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Right) || (IsDiagonalDoubledPhase(weapon.y) && MoveWeaponAndTestHit(OrthogonalDirection_Right)))
				break;
			goto case_MoveDirection_Up;
		case MoveDirection_DownRight:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Down) || (IsDiagonalDoubledPhase(weapon.y) && MoveWeaponAndTestHit(OrthogonalDirection_Right)))
				break;
			// fall through
		case MoveDirection_Right:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Right))
				break;
			goto main_139A;
		case MoveDirection_Down:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Down))
				break;
			goto main_139A;
		case MoveDirection_DownLeft:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Down) || (IsDiagonalDoubledPhase(weapon.y) && MoveWeaponAndTestHit(OrthogonalDirection_Left)))
				break;
			// fall through
		case MoveDirection_Left:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Left))
				break;
			goto main_139A;
		case MoveDirection_UpLeft:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Left) || (IsDiagonalDoubledPhase(weapon.y) && MoveWeaponAndTestHit(OrthogonalDirection_Left)))
				break;
			// fall through
		case MoveDirection_Up:
		case_MoveDirection_Up:
			if (MoveWeaponAndTestHit(OrthogonalDirection_Up))
				break;
		main_139A:
			if (weapon.weaponType!=WeaponType_Bullet)
				currentSprite = FakePointerToPointer(weapon.sprite);
			else
			{
				if (++weapon.animFrame > 3)
					weapon.animFrame = 0;
				currentSprite = data_11D4[weapon.animFrame];
			}
			*weaponTestPos = (MazeTile&)currentSprite[1];
			prevObject = object;
			object = weapon.next;
			continue;
		default:
			UNREACHABLE;
		}
		BYTE find_this = weaponTestPos->chr;
		if (!memchr(mazeWallCharacters, find_this, _countof(mazeWallCharacters)))
		{
			if (weapon.weaponType==WeaponType_Bullet)
			{
				if (memchr(enemyCharacters, find_this, _countof(enemyCharacters)))
					score += 1;
				else
				if (IsSnipePortal(*weaponTestPos))
					score += 50;
			}
			else
			if (snipePortalsResistSnipeSpears && IsSnipePortal(*weaponTestPos))
				goto main_150E;
			*weaponTestPos = MazeTile(0xF, 0xB2);
		}
		else
		if (enableBouncingBullets && weapon.weaponType==WeaponType_Bullet && weaponBouncesRemaining[object] && (weapon.moveDirection & MoveDirectionMask_Diagonal))
		{
			weaponBouncesRemaining[object]--;
			weapon.moveDirection = (MoveDirection)bulletBounceTable[weaponCollisionDirection][weapon.moveDirection];
			SetSoundEffectState(1, SoundEffect_Bullet);
			MoveWeaponAndTestHit((weaponCollisionDirection + 2) & OrthogonalDirectionMask_All);
			goto main_139A;
		}
	main_150E:
		numWeapons--;
		if (!prevObject)
			objectHead_weapons = weapon.next;
		else
			objects[prevObject].next = weapon.next;
		ObjectIndex nextObject = weapon.next;
		FreeObject(object);
		object = nextObject;
	}
}

struct OrthoDistanceInfo
{
	union
	{
		struct {BYTE x, y;};
		Coord xy;
	};
	MoveDirection direction;
};

OrthoDistanceInfo GetOrthoDistanceAndDirection(Object &object)
// Calculates the orthogonal distance between object and the player
{
	OrthoDistanceInfo result;
	BYTE bx = 1;
	int  ax = object.x - viewportFocusX;
	if (ax <= 0)
	{
		bx = 0;
		ax = -ax;
	}
	if (ax >= MAZE_WIDTH/2)
	{
		bx ^= 1;
		ax = MAZE_WIDTH - ax;
	}
	result.x = ax;
	ax = object.y - viewportFocusY;
	if (ax < 0)
	{
		bx += 2;
		ax = -ax;
	}
	if (ax >= MAZE_HEIGHT/2)
	{
		bx ^= 2;
		ax = MAZE_HEIGHT - ax;
	}
	result.y = ax;
	orthoDistance = result.x + result.y;
	if (result.x == 0)
	{
		result.direction = (MoveDirection)(bx * 2);
		return result;
	}
	if (result.y == 0)
	{
		result.direction = (MoveDirection)(bx * 4 + 2);
		return result;
	}
	static const MoveDirection diagonalDirectionTable[] = {MoveDirection_UpRight, MoveDirection_UpLeft, MoveDirection_DownRight, MoveDirection_DownLeft};
	result.direction = diagonalDirectionTable[bx];
	return result;
}

struct MoveObject_retval {bool hitObstruction; BYTE chrHit; Coord cx; MazeTile *bx_si;};
MoveObject_retval MoveObject(MovingObject &object)
{
	Coord ax, cx, dx;
	ax = cx = object.xy;
	dx = 0;
	int tmp;
	switch (object.moveDirection)
	{
	case MoveDirection_UpRight:
		tmp = cx.x + (dx.x = IsDiagonalDoubledPhase(cx.y) + 1);
		if (tmp >= MAZE_WIDTH)
			tmp -= MAZE_WIDTH;
		cx.x = tmp;
		// fall through
	case MoveDirection_Up:
		dx.y++;
		tmp = cx.y - 1;
		if (tmp < 0)
			tmp = MAZE_HEIGHT - 1;
		ax.y = cx.y = tmp;
		break;
	case MoveDirection_Right:
		dx.x++;
		tmp = cx.x + 1;
		if (tmp >= MAZE_WIDTH)
			tmp = 0;
		cx.x = tmp;
		break;
	case MoveDirection_DownRight:
		dx.y++;
		tmp = cx.y + 1;
		if (tmp >= MAZE_HEIGHT)
			tmp = 0;
		cx.y = tmp;
		tmp = cx.x + (dx.x = IsDiagonalDoubledPhase(cx.y) + 1);
		if (tmp >= MAZE_WIDTH)
			tmp -= MAZE_WIDTH;
		cx.x = tmp;
		break;
	case MoveDirection_Down:
		dx.y++;
		tmp = cx.y + 1;
		if (tmp >= MAZE_HEIGHT)
			tmp = 0;
		cx.y = tmp;
		break;
	case MoveDirection_DownLeft:
		dx.y++;
		tmp = cx.y + 1;
		if (tmp >= MAZE_HEIGHT)
			tmp = 0;
		cx.y = tmp;
		tmp = cx.x - (dx.x = IsDiagonalDoubledPhase(cx.y) + 1);
		if (tmp < 0)
			tmp += MAZE_WIDTH;
		ax.x = cx.x = tmp;
		break;
	case MoveDirection_Left:
		dx.x++;
		tmp = cx.x - 1;
		if (tmp < 0)
			tmp = MAZE_WIDTH - 1;
		ax.x = cx.x = tmp;
		break;
	case MoveDirection_UpLeft:
		tmp = cx.x - (dx.x = IsDiagonalDoubledPhase(cx.y) + 1);
		if (tmp < 0)
			tmp += MAZE_WIDTH;
		cx.x = tmp;
		dx.y++;
		tmp = cx.y - 1;
		if (tmp < 0)
			tmp = MAZE_HEIGHT - 1;
		cx.y = tmp;
		ax = cx;
		break;
	default:
		UNREACHABLE;
	}
	const WORD *ptr = FakePointerToPointer(object.sprite);
	dx.x += ((BYTE*)ptr)[1];
	dx.y += ((BYTE*)ptr)[0];
	for (MazeTile *mazeRow = &maze[ax.y * MAZE_WIDTH];;)
	{
		BYTE countX = dx.x;
		for (size_t x = ax.x;;)
		{
			if (mazeRow[x].chr != ' ')
			{
				MoveObject_retval retval;
				retval.hitObstruction = true;
				retval.chrHit = mazeRow[x].chr;
				retval.cx = cx;
				retval.bx_si = &mazeRow[x];
				return retval;
			}
			if (--countX == 0)
				break;
			if (++x >= MAZE_WIDTH)
				x = 0;
		}
		if (--dx.y == 0)
			break;
		mazeRow += MAZE_WIDTH;
		if (mazeRow >= &maze[_countof(maze)])
			mazeRow -=       _countof(maze);
	}
	MoveObject_retval retval;
	retval.hitObstruction = false;
	retval.chrHit = 0;
	retval.cx = cx;
	return retval;
}

void FireWeapon(BYTE weaponType)
{
	MovingObject &shooter = *(MovingObject*)currentObject;
	MoveDirection fireDirection = shooter.moveDirection;
	BYTE x = shooter.x;
	BYTE y = shooter.y;
	switch (fireDirection)
	{
	case MoveDirection_UpRight:
		x += ((BYTE*)currentSprite)[1];
		goto case_MoveDirection_Up;
	case MoveDirection_Right:
		x += ((BYTE*)currentSprite)[1];
		break;
	case MoveDirection_DownRight:
		x += ((BYTE*)currentSprite)[1];
		goto main_168F;
	case MoveDirection_Down:
		y += ((BYTE*)currentSprite)[0];
		x += ((BYTE*)currentSprite)[1] - 1;
		break;
	case MoveDirection_DownLeft:
		x--;
	main_168F:
		y += ((BYTE*)currentSprite)[0];
		break;
	case MoveDirection_Left:
		x--;
		break;
	case MoveDirection_UpLeft:
		x--;
		// fall through
	case MoveDirection_Up:
	case_MoveDirection_Up:
		y--;
		break;
	default:
		UNREACHABLE;
	}
	if (x >= MAZE_WIDTH)
	{
		if (x > 240)
			x += MAZE_WIDTH;
		else
			x -= MAZE_WIDTH;
	}
	if (y >= MAZE_HEIGHT)
	{
		if (y > 240)
			y += MAZE_HEIGHT;
		else
			y -= MAZE_HEIGHT;
	}
	const WORD *currentSprite_backup = currentSprite;
	currentSprite = data_1150;
	if (IsObjectLocationOccupied(y, x))
	{
		MazeTile &weaponHitPos = maze[y * MAZE_WIDTH + x];
		if (!memchr(mazeWallCharacters, weaponHitPos.chr, _countof(mazeWallCharacters)))
		{
			if (weaponType==WeaponType_Bullet)
			{
				if (memchr(enemyCharacters, weaponHitPos.chr, _countof(enemyCharacters)))
					score += 1;
				else
				if (IsSnipePortal(weaponHitPos))
					score += 50;
				weaponHitPos = MazeTile(0xF, 0xB2);
			}
			else
			if (!(snipePortalsResistSnipeSpears && IsSnipePortal(weaponHitPos)))
				weaponHitPos = MazeTile(0xF, 0xB2);
		}
	}
	else
	{
		ObjectIndex newWeapon = CreateNewObject();
		if (newWeapon && numWeapons <= 50)
		{
			numWeapons++;
			Object *currentObject_backup = currentObject;
			Weapon &weapon = SetCurrentObject<Weapon>(newWeapon);
			if (!objectHead_weapons)
				objectHead_weapons = newWeapon;
			else
			{
				ObjectIndex objectTail_weapons = objectHead_weapons;
				while (ObjectIndex nextObject = objects[objectTail_weapons].next)
					objectTail_weapons = nextObject;
				objects[objectTail_weapons].next = newWeapon;
			}
			weapon.next = 0;
			if (weaponType==WeaponType_Bullet)
				weapon.sprite = FAKE_POINTER(1150);
			else
				weapon.sprite = PointerToFakePointer(data_11B4[fireDirection]);
			weapon.x = x;
			weapon.y = y;
			weapon.moveDirection = fireDirection;
			weapon.animFrame = 0;
			weapon.weaponType = weaponType;
			weaponBouncesRemaining[newWeapon] = (BYTE)GetRandomMasked(7) + 1;
			currentSprite = FakePointerToPointer(weapon.sprite);
			PlotObjectToMaze();
			currentObject = currentObject_backup;
		}
	}
	currentSprite = currentSprite_backup;
}

bool FireSnipeSpear()
{
	BYTE shiftCount = orthoDistance >> snipeShootingAccuracy;
	if (shiftCount > 10)
		return false;
	if (GetRandomMasked(0xFFFF >> (15 - shiftCount)))
		return false;
	FireWeapon(WeaponType_Spear);
	SetSoundEffectState(0, SoundEffect_Spear);
	return true;
}

void UpdateLargeSnipes()
{
	for (ObjectIndex object = objectHead_largeSnipes; object;)
	{
		LargeSnipe &snipe = ((LargeSnipe*)objects)[object];
		MazeTile *snipeMazeRow = &maze[snipe.y * MAZE_WIDTH];
		MazeTile * leftPart = &snipeMazeRow[snipe.x];
		MazeTile *rightPart = snipe.x >= MAZE_WIDTH-1 ? &snipeMazeRow[0] : leftPart + 1;
		MazeTile *smallSnipePart;
		if (leftPart->chr == 0xB2)
		{
			*leftPart = MazeTile(0x9, ' ');
			smallSnipePart = rightPart;
		}
		else
		if (rightPart->chr == 0xB2)
		{
			*rightPart = MazeTile(0x9, ' ');
			smallSnipePart = leftPart;
		}
		else
		if (--snipe.moveFrame == 0)
		{
			* leftPart = MazeTile(0x9, ' ');
			*rightPart = MazeTile(0x9, ' ');
			if (GetRandomMasked(3) != 0) // 3/4 chance of moving in the same direction as before
			{
				MoveObject_retval result = MoveObject(snipe);
				if (!result.hitObstruction)
				{
					snipe.xy = result.cx;
					if (!(snipe.moveDirection & MoveDirectionMask_Diagonal))
						goto main_2021;
					snipe.moveFrame = 8;
					goto main_2025;
				}
			}
			if (GetRandomMasked(3) == 0) // 1/4 * 1/4 chance of moving in a random direction
				snipe.moveDirection = (MoveDirection)GetRandomMasked(MoveDirectionMask_All);
			else                         // 1/4 * 3/4 chance of moving toward the player
				snipe.moveDirection = GetOrthoDistanceAndDirection(snipe).direction;
			for (Uint count=8; count; count--)
			{
				MoveObject_retval result = MoveObject(snipe);
				if (!result.hitObstruction)
					break;
				if (snipe.movementFlags & EnemyMovementFlag_TurnDirection)
					snipe.moveDirection = (snipe.moveDirection - 1) & MoveDirectionMask_All;
				else
					snipe.moveDirection = (snipe.moveDirection + 1) & MoveDirectionMask_All;
			}
			snipe.sprite = PointerToFakePointer(data_1130[snipe.moveDirection]);
		main_2021:
			snipe.moveFrame = 6;
		main_2025:
			const WORD *sprite = FakePointerToPointer(snipe.sprite);
			maze[snipe.y * MAZE_WIDTH + snipe.x] = (MazeTile&)sprite[1 + 0];
			if (snipe.x < MAZE_WIDTH-1)
				maze[snipe.y * MAZE_WIDTH + snipe.x+1] = (MazeTile&)sprite[1 + 1];
			else
				maze[snipe.y * MAZE_WIDTH            ] = (MazeTile&)sprite[1 + 1];
			OrthoDistanceInfo orthoDist = GetOrthoDistanceAndDirection(snipe);
			MoveDirection al = orthoDist.direction;
			MoveDirection ah = snipe.moveDirection;
			if (al != ah)
				goto main_209E;
			if (orthoDist.x && orthoDist.y)
			{
				if (abs((int)orthoDist.x * MAZE_CELL_HEIGHT - (int)orthoDist.y * MAZE_CELL_WIDTH) >= MAZE_CELL_WIDTH)
					goto next_snipe;
				al = snipe.moveDirection;
			}
			for (;;)
			{
				{
					MoveDirection moveDirection = snipe.moveDirection;
					snipe.moveDirection = al;
					currentObject = (Object*)&snipe;
					currentSprite = FakePointerToPointer(snipe.sprite);
					bool tmp = FireSnipeSpear();
					snipe.moveDirection = moveDirection;
					if (!tmp)
						break;
				}
				if (maze[snipe.y * MAZE_WIDTH + snipe.x].chr != 0x01)
					maze[snipe.y * MAZE_WIDTH + snipe.x] = MazeTile(0x9, 0xFF);
				else
				{
					if (snipe.x < MAZE_WIDTH-1)
						maze[snipe.y * MAZE_WIDTH + snipe.x+1] = MazeTile(0x9, 0xFF);
					else
						maze[snipe.y * MAZE_WIDTH            ] = MazeTile(0x9, 0xFF);
				}
				break;
			main_209E:
				if (orthoDist.y <= 2 && al != MoveDirection_Up && al != MoveDirection_Down)
				{
					if (al <= MoveDirection_Down)
					{
						if (inrange(ah, MoveDirection_UpRight, MoveDirection_DownRight))
						{
							al = MoveDirection_Right;
							continue;
						}
					}
					else
					if (ah >= MoveDirection_DownLeft)
					{
						al = MoveDirection_Left;
						continue;
					}
				}
				if (orthoDist.x > 2)
					break;
				al = (al + 1) & MoveDirectionMask_All;
				if (al == MoveDirection_UpLeft || al == MoveDirection_DownRight)
					break;
				if (al < MoveDirection_Down)
				{
					ah = (ah + 1) & MoveDirectionMask_All;
					if (ah > MoveDirection_Right)
						break;
					al = MoveDirection_Up;
					continue;
				}
				ah = (ah + 2) & MoveDirectionMask_All;
				if (ah <= MoveDirection_Down)
					break;
				al = MoveDirection_Down;
			}
			goto next_snipe;
		}
		else
		{
		next_snipe:
			object = snipe.next;
			continue;
		}
		if (enableSmallSnipes && smallSnipePart->chr == 0x01)
		{
			*smallSnipePart = MazeTile(0x5, 0x02);
			SmallSnipe &smallSnipe = (SmallSnipe&)snipe;
			smallSnipe.x = smallSnipePart - snipeMazeRow;

			// move this object out of the LargeSnipe linked-list and into the SmallSnipe linked-list
			for (ObjectIndex *nextPtr = &objectHead_largeSnipes;; nextPtr = &objects[*nextPtr].next)
			{
				if (*nextPtr == object)
				{
					ObjectIndex nextObject = *nextPtr = smallSnipe.next;
					smallSnipe.next = objectHead_smallSnipes;
					objectHead_smallSnipes = object;
					object = nextObject;
					break;
				}
			}
			smallSnipe.moveFrame = 2;
			smallSnipe.sprite = FAKE_POINTER(10FE);
			numSmallSnipes++;
		}
		else
		{
			ObjectIndex nextObject = snipe.next;
			FreeObjectInList(&objectHead_largeSnipes, object);
			object = nextObject;
		}
		numLargeSnipes--;
		numLargeSnipesDestroyed++;
	}
}

void UpdateSmallSnipes()
{
	for (ObjectIndex object = objectHead_smallSnipes; object;)
	{
		SmallSnipe &smallSnipe = ((SmallSnipe*)objects)[object];
		MazeTile &smallSnipeInMaze = maze[smallSnipe.y * MAZE_WIDTH + smallSnipe.x];
		if (smallSnipeInMaze.chr != 0xB2)
		{
			if (--smallSnipe.moveFrame)
			{
				object = smallSnipe.next;
				continue;
			}
		}
		else
		{
	destroy_smallSnipe:
			ObjectIndex nextObject = smallSnipe.next;
			FreeObjectInList(&objectHead_smallSnipes, object);
			object = nextObject;
			numSmallSnipes--;
			numSmallSnipesDestroyed++;
			continue;
		}
		smallSnipeInMaze = MazeTile(0x9, ' ');
		if (!(smallSnipe.movementFlags & EnemyMovementFlag_SmallSnipeMoveStraight))
		{
			OrthoDistanceInfo orthoDist = GetOrthoDistanceAndDirection(smallSnipe);
			if (orthoDistance <= 4)
			{
				smallSnipe.moveDirection = orthoDist.direction;
				MoveObject_retval result = MoveObject(smallSnipe);
				if (result.hitObstruction && (smallSnipe.moveDirection & MoveDirectionMask_Diagonal))
					if (IsPlayer(result.chrHit))
					{
						if (GetRandomMasked(smallSnipeExplosionChance) == 0)
						{
							result.bx_si->chr = 0xB2;
							goto destroy_smallSnipe;
						}
#if !defined(FIX_BUGS) && !defined(FIX_SMALL_SNIPE_LOGIC_COMPILER_BUG)
						// A PL/M-86 compiler bug manifesting in the original game caused the CX register to be overwritten by the call to GetRandomMasked().
						// When a small snipe has a chance to explode next to the player, but doesn't, this bug changes the way it will move.
						orthoDist.xy = 947;
#endif
					}
			}
			if (orthoDist.y == 1)
				orthoDist.direction = orthoDist.direction >= MoveDirection_Down ? MoveDirection_Left : MoveDirection_Right;
			else
			if (orthoDist.y < 1)
			{
				smallSnipe.moveDirection = ++orthoDist.direction;
				MoveObject_retval result = MoveObject(smallSnipe);
				if (!result.hitObstruction)
				{
					smallSnipe.xy = result.cx;
					goto plot_smallSnipe_and_continue;
				}
				orthoDist.direction -= 2;
			}
			else
			if (orthoDist.x == 1)
				orthoDist.direction = (orthoDist.direction + 1) & MoveDirectionMask_UpDown;
			else
			if (orthoDist.x < 1)
			{
				smallSnipe.moveDirection = orthoDist.direction += 2;
				MoveObject_retval result = MoveObject(smallSnipe);
				if (!result.hitObstruction)
				{
					smallSnipe.xy = result.cx;
					goto plot_smallSnipe_and_continue;
				}
				orthoDist.direction = (orthoDist.direction - 4) & MoveDirectionMask_All;
			}
			smallSnipe.moveDirection = orthoDist.direction;
			{
				MoveObject_retval result = MoveObject(smallSnipe);
				if (!result.hitObstruction)
				{
					smallSnipe.xy = result.cx;
					goto plot_smallSnipe_and_continue;
				}
			}
			if (orthoDistance >= 20)
				smallSnipe.movementFlags |= EnemyMovementFlag_SmallSnipeMoveStraight;
			smallSnipe.moveDirection = (MoveDirection)GetRandomMasked(MoveDirectionMask_All);
		}
		for (Uint count=8; count; count--)
		{
			MoveObject_retval result = MoveObject(smallSnipe);
			if (!result.hitObstruction)
			{
				smallSnipe.xy = result.cx;
				break;
			}
			smallSnipe.movementFlags &= ~EnemyMovementFlag_SmallSnipeMoveStraight;
			if (smallSnipe.movementFlags & EnemyMovementFlag_TurnDirection)
				smallSnipe.moveDirection = (smallSnipe.moveDirection - 1) & MoveDirectionMask_All;
			else
				smallSnipe.moveDirection = (smallSnipe.moveDirection + 1) & MoveDirectionMask_All;
		}
	plot_smallSnipe_and_continue:
		maze[smallSnipe.y * MAZE_WIDTH + smallSnipe.x] = MazeTile(0x5, 0x02);
		smallSnipe.moveFrame = 3;
		object = smallSnipe.next;
	}
}

bool IsObjectTaggedToExplode()
{
	BYTE spriteHeight = ((BYTE*)currentSprite)[0];
	BYTE spriteWidth  = ((BYTE*)currentSprite)[1];
	MazeTile *mazeTile = &maze[currentObject->y * MAZE_WIDTH];
	for (BYTE row = 0; row < spriteHeight; row++)
	{
		BYTE x = currentObject->x;
		for (BYTE column = 0; column < spriteWidth; column++)
		{
			if (mazeTile[x].chr == 0xB2)
				return true;
			if (++x >= MAZE_WIDTH)
				x = 0;
		}
		mazeTile += MAZE_WIDTH;
		if (mazeTile >= &maze[_countof(maze)])
			mazeTile -=       _countof(maze);
	}
	return false;
}

void EraseObjectFromMaze()
{
	BYTE spriteHeight = ((BYTE*)currentSprite)[0];
	BYTE spriteWidth  = ((BYTE*)currentSprite)[1];
	MazeTile *mazeTile = &maze[currentObject->y * MAZE_WIDTH];
	for (BYTE row = 0; row < spriteHeight; row++)
	{
		BYTE x = currentObject->x;
		for (BYTE column = 0; column < spriteWidth; column++)
		{
			mazeTile[x] = MazeTile(0x9, ' ');
			if (++x >= MAZE_WIDTH)
				x = 0;
		}
		mazeTile += MAZE_WIDTH;
		if (mazeTile >= &maze[_countof(maze)])
			mazeTile -=       _countof(maze);
	}
}

//template <typename TYPE> TYPE &IncWrap(TYPE &n, 

void UpdateSnipePortals()
{
	for (ObjectIndex object = objectHead_snipePortals; object;)
	{
		SnipePortal &snipePortal = SetCurrentObject<SnipePortal>(object);
		if (++snipePortal.animFrame >= _countof(data_10A2))
			snipePortal.animFrame = 0;
		currentSprite = data_10A2[snipePortal.animFrame];
		snipePortal.sprite = PointerToFakePointer(currentSprite);
		if (IsObjectTaggedToExplode())
		{
			ObjectIndex nextObject = snipePortal.next;
			FreeObjectInList(&objectHead_snipePortals, object);
			object = nextObject;
			numSnipePortals--;
			continue;
		}
		EraseObjectFromMaze();
		PlotObjectToMaze();
		if (--snipePortal.spawnFrame == 0)
		{
			GetOrthoDistanceAndDirection(*currentObject);
			if (frame >= 0xF00)
				snipePortal.spawnFrame = 5;
			else
				snipePortal.spawnFrame = 5 + (orthoDistance >> (frame/0x100 + 1));
			if (GetRandomMasked(0xF >> (numSnipePortalsAtStart - numSnipePortals)) == 0)
			{
				currentSprite = data_1112;
				BYTE x = snipePortal.x + 2;
#ifdef EMULATE_LATENT_BUGS
				if (x  > MAZE_WIDTH - 1)
					x -= MAZE_WIDTH - 1;
#else
				if (x >= MAZE_WIDTH)
					x -= MAZE_WIDTH;
#endif
				BYTE y = snipePortal.y;
				if (!IsObjectLocationOccupied(y, x))
				{
					if (numLargeSnipes + numSmallSnipes < maxSnipes)
					{
						ObjectIndex spawnedSnipeIndex = CreateNewObject();
						if (spawnedSnipeIndex)
						{
							numLargeSnipes++;
							object = snipePortal.next;
							LargeSnipe &spawnedSnipe = SetCurrentObject<LargeSnipe>(spawnedSnipeIndex);
							spawnedSnipe.next = objectHead_largeSnipes;
							objectHead_largeSnipes = spawnedSnipeIndex;
							spawnedSnipe.x = x;
							spawnedSnipe.y = y;
							spawnedSnipe.moveDirection = MoveDirection_Right;
							spawnedSnipe.sprite = FAKE_POINTER(1112);
							PlotObjectToMaze();
							spawnedSnipe.movementFlags = (BYTE)GetRandomMasked(1); // randomly set or clear EnemyMovementFlag_TurnDirection
							spawnedSnipe.moveFrame = 4;
							continue;
						}
					}
				}
			}
		}
		object = snipePortal.next;
	}
}

bool MovePlayer_helper(bool &hitObstruction, OrthogonalDirection arg)
{
	player.moveDirection = OrthoDirectionToMoveDirection(arg);
	MoveObject_retval result = MoveObject(*(MovingObject*)currentObject);
	if (result.hitObstruction)
		return hitObstruction = true;
	viewportFocusX = player.x = result.cx.x;
	viewportFocusY = player.y = result.cx.y;
	return false;
}

bool MovePlayer()
{
	MoveDirection moveDirection = player.moveDirection;
	bool hitObstruction = false;
	switch (moveDirection)
	{
	case MoveDirection_UpRight:
		MovePlayer_helper(hitObstruction, OrthogonalDirection_Right);
		if (!IsDiagonalDoubledPhase(currentObject->y))
			goto case_MoveDirection_Up;
		if (MovePlayer_helper(hitObstruction, OrthogonalDirection_Right))
			goto case_MoveDirection_Up;
		if (!MovePlayer_helper(hitObstruction, OrthogonalDirection_Up))
			break;
		goto case_MoveDirection_Left;
	case MoveDirection_DownRight:
		if (MovePlayer_helper(hitObstruction, OrthogonalDirection_Down))
			goto case_MoveDirection_Right;
		if (IsDiagonalDoubledPhase(currentObject->y))
			MovePlayer_helper(hitObstruction, OrthogonalDirection_Right);
		goto case_MoveDirection_Right;
	case MoveDirection_Down:
		MovePlayer_helper(hitObstruction, OrthogonalDirection_Down);
		break;
	case MoveDirection_DownLeft:
		if (MovePlayer_helper(hitObstruction, OrthogonalDirection_Down))
			goto case_MoveDirection_Left;
		if (!IsDiagonalDoubledPhase(currentObject->y))
			goto case_MoveDirection_Left;
		MovePlayer_helper(hitObstruction, OrthogonalDirection_Left);
		// fall through
	case MoveDirection_Left:
	case_MoveDirection_Left:
		MovePlayer_helper(hitObstruction, OrthogonalDirection_Left);
		break;
	case MoveDirection_UpLeft:
		MovePlayer_helper(hitObstruction, OrthogonalDirection_Left);
		if (!IsDiagonalDoubledPhase(currentObject->y))
			goto case_MoveDirection_Up;
		if (MovePlayer_helper(hitObstruction, OrthogonalDirection_Left))
			goto case_MoveDirection_Up;
		if (!MovePlayer_helper(hitObstruction, OrthogonalDirection_Up))
			break;
		// fall through
	case MoveDirection_Right:
	case_MoveDirection_Right:
		MovePlayer_helper(hitObstruction, OrthogonalDirection_Right);
		break;
	case MoveDirection_Up:
	case_MoveDirection_Up:
		MovePlayer_helper(hitObstruction, OrthogonalDirection_Up);
		break;
	default:
		UNREACHABLE;
	}
	if (!hitObstruction)
		PlotObjectToMaze();
	player.moveDirection = moveDirection;
	return !hitObstruction;
}

bool UpdatePlayer(bool playbackMode, BYTE &replayIO) // returns true if the match has been lost
{
	if (!playbackMode)
		replayIO = 0;
	currentObject = (Object*)&player;
	if (++playerEyeAnimFrame > 7)
	{
		playerEyeAnimFrame = 0;
		playerAnimEyesNotWide ^= true;
	}
	if (playerAnimEyesNotWide)
		currentSprite = data_10E2;
	else
		currentSprite = data_10EC;
	if (isPlayerDying)
	{
		keyboard_state = PollKeyboard();
		if (numPlayerDeaths >= numLives)
		{
			DrawViewport();
			EraseBottomTwoLines();
			CloseDirectConsole(WINDOW_HEIGHT-2);
			WriteTextToConsole("The SNIPES have triumphed!!!\r\n");
			return true;
		}
		if (!isPlayerExploding)
		{
			keyboard_state = PollKeyboard();
			isPlayerDying = false;
			PlaceObjectInRandomUnoccupiedMazeCell();
			viewportFocusX = player.x;
			viewportFocusY = player.y;
			PlotObjectToMaze();
			return false;
		}
		return false;
	}
	else
	{
		if (--player.inputFrame)
		{
			if (IsObjectTaggedToExplode())
				goto explode_player;
			return false;
		}
		keyboard_state = PollKeyboard();
		player.inputFrame = 2;
		if (IsObjectTaggedToExplode())
			goto explode_player;
	}
	EraseObjectFromMaze();
	static const BYTE arrowKeyMaskToDirectionTable[] =
	{
		0,
		MoveDirection_Right,
		MoveDirection_Left,
		0,
		MoveDirection_Down,
		MoveDirection_DownRight,
		MoveDirection_DownLeft,
		0,
		MoveDirection_Up,
		MoveDirection_UpRight,
		MoveDirection_UpLeft,
		0,
		0,
		0,
		0,
		0,
	};
	BYTE moveDirection, keyboardMove;
	if (playbackMode)
	{
		spacebar_state = replayIO >> 7;
		moveDirection = (replayIO & 0x7F) % 9;
		if (moveDirection)
		{
			moveDirection--;
			goto playback_move;
		}
	}
	else
	if ((keyboardMove = keyboard_state & (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT | KEYSTATE_MOVE_DOWN | KEYSTATE_MOVE_UP)))
	{
		moveDirection = arrowKeyMaskToDirectionTable[keyboardMove];
playback_move:
		player.moveDirection = (MoveDirection)moveDirection;
		if (!playbackMode)
			replayIO = moveDirection + 1;
		if (MovePlayer())
		{
			if (!spacebar_state)
				goto main_1B8F;
			if (!playbackMode)
				replayIO += 0x80;
			if (player.inputFrame == 1)
			{
				EraseObjectFromMaze();
				if (!MovePlayer())
				{
					if (enableElectricWalls)
						goto explode_player;
					PlotObjectToMaze();
				}
			}
			player.inputFrame = 1;
			goto main_1B8F;
		}
		if (enableElectricWalls)
			goto explode_player;
	}
	PlotObjectToMaze();
main_1B8F:
	BYTE fireDirection;
	if (playbackMode)
	{
		fireDirection = (replayIO & 0x7F) / 9 % 9;
		if (fireDirection)
		{
			fireDirection--;
			goto playback_fire;
		}
	}
	else
	if (!spacebar_state && (keyboard_state & (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT | KEYSTATE_FIRE_DOWN | KEYSTATE_FIRE_UP)))
	{
		fireDirection = arrowKeyMaskToDirectionTable[keyboard_state >> 4];
playback_fire:
		if (!playbackMode)
			replayIO += (fireDirection + 1) * 9;
		if (--player.firingFrame)
			return false;
		MoveDirection moveDirection_backup = player.moveDirection;
		player.moveDirection = (MoveDirection)fireDirection;
		FireWeapon(WeaponType_Bullet);
		SetSoundEffectState(0, SoundEffect_Bullet);
		player.moveDirection = moveDirection_backup;
		player.firingFrame = player.inputFrame == 1 ? playerFiringPeriod<<1 : playerFiringPeriod;
		return false;
	}
	player.firingFrame = 1;
	return false;
explode_player:
	FreeObjectInList(&player.next, OBJECT_PLAYER); // explode the player
	isPlayerDying = true;
	numPlayerDeaths++;
	return false;
}

void UpdateExplosions()
{
	for (ObjectIndex object = objectHead_explosions; object;)
	{
		Explosion &explosion = SetCurrentObject<Explosion>(object);
		currentSprite = FakePointerToPointer(explosion.sprite);
		EraseObjectFromMaze();
		BYTE animFrame = (explosion.animFrame + 1) % 6;
		ObjectIndex nextObject = explosion.next;
		explosion.animFrame++;
		if (explosion.animFrame > (object == OBJECT_PLAYEREXPLOSION ? 11 : 5))
		{
			FreeObjectInList_worker(&objectHead_explosions, object);
			if (object != OBJECT_PLAYEREXPLOSION)
				FreeObject(object);
			else
				isPlayerExploding = false;
		}
		else
		{
			if (explosion.spriteSize == EXPLOSION_SIZE(2,2))
			{
				explosion.sprite = PointerToFakePointer(data_12FE[animFrame]);
				currentSprite = data_12FE[animFrame];
				if (object == OBJECT_PLAYEREXPLOSION && explosion.animFrame >= 6)
					SetSoundEffectState(5 - (explosion.animFrame - 6), SoundEffect_ExplodePlayer);
				else
					SetSoundEffectState(animFrame, SoundEffect_ExplodePlayer);
			}
			else
			if (explosion.spriteSize == EXPLOSION_SIZE(2,1))
			{
				explosion.sprite = PointerToFakePointer(data_1352[animFrame]);
				currentSprite = data_1352[animFrame];
				SetSoundEffectState(animFrame, SoundEffect_ExplodeLargeSnipe);
			}
			else
			if (explosion.spriteSize == EXPLOSION_SIZE(1,1))
			{
				explosion.sprite = PointerToFakePointer(data_1392[animFrame]);
				currentSprite = data_1392[animFrame];
				SetSoundEffectState(animFrame, SoundEffect_ExplodeSmallSnipe);
			}
			PlotObjectToMaze();
		}
		object = nextObject;
	}
}

static const WORD sound_ExplodeSmallSnipe[] = { 100,  100, 1400, 1800, 1600, 1200};
static const WORD sound_ExplodeLargeSnipe[] = {2200, 6600, 1800, 4400, 8400, 1100};
static const WORD sound_ExplodePlayer    [] = {2000, 8000, 6500, 4000, 2500, 1000};

void UpdateSound()
{
	if (!sound_enabled)
	{
		ClearSound();
		return;
	}
	if (currentSoundEffect == SoundEffect_None)
	{
#ifdef STOP_WAVE_OUT_DURING_SILENCE
		PlayTone(-1);
#else
		PlayTone(0);
#endif
		return;
	}
	switch (currentSoundEffect)
	{
	case SoundEffect_Bullet:
		if (!currentSoundEffectFrame)
			PlayTone(1900);
		else
			PlayTone(1400);
		break;
	case SoundEffect_Spear:
		PlayTone(1600);
		break;
	case SoundEffect_ExplodeSmallSnipe:
		PlayTone(sound_ExplodeSmallSnipe[currentSoundEffectFrame]);
		break;
	case SoundEffect_ExplodeLargeSnipe:
		PlayTone(sound_ExplodeLargeSnipe[currentSoundEffectFrame]);
		break;
	case SoundEffect_ExplodePlayer:
		PlayTone(sound_ExplodePlayer[currentSoundEffectFrame]);
		break;
	default:
		UNREACHABLE;
	}
	currentSoundEffect = SoundEffect_None;
}

void DrawViewport()
{
	WORD outputRow = VIEWPORT_ROW;
	SHORT data_298 = viewportFocusY - VIEWPORT_HEIGHT / 2;
	if (data_298 < 0)
		data_298 += MAZE_HEIGHT;
	SHORT data_296 = viewportFocusX - WINDOW_WIDTH / 2;
	if (data_296 < 0)
		data_296 += MAZE_WIDTH;
	SHORT wrappingColumn = 0;
	if (data_296 + WINDOW_WIDTH >= MAZE_WIDTH)
		wrappingColumn = MAZE_WIDTH - data_296;
	else
		wrappingColumn = WINDOW_WIDTH;
	for (Uint row = 0; row < VIEWPORT_HEIGHT; row++)
	{
		WORD data_29C = data_298 * MAZE_WIDTH;
		WriteTextMem(wrappingColumn, outputRow, 0, &maze[data_296 + data_29C]);
		if (wrappingColumn != WINDOW_WIDTH)
			WriteTextMem(WINDOW_WIDTH - wrappingColumn, outputRow, wrappingColumn, &maze[data_29C]);
		outputRow++;
		if (++data_298 == MAZE_HEIGHT)
			data_298 = 0;
	}
}

void WaitForNextTick(WORD &tick_count)
{
	for (;;)
	{
		SleepTimeslice();
		WORD tick_count2 = GetTickCountWord();
		if (tick_count2 != tick_count)
		{
			tick_count = tick_count2;
			break;
		}
	}
}

#if (defined(_WIN32) || defined(_WIN64)) && !defined(_CONSOLE)
extern "C" int __cdecl SDL_main(int argc, char* argv[])
#else
int __cdecl main(int argc, char* argv[])
#endif
{
	if (argc > 2)
	{
		fprintf(stderr, "Usage: %s [filename of replay to play back]\n", argv[0]);
		return -1;
	}
	bool playbackMode;
	const char *replayPlaybackFilename = NULL;
	if ((playbackMode = (argc == 2)))
		replayPlaybackFilename = argv[1];

	if (int result = OpenConsole())
		return result;
	
	// Create SDL window on main thread for macOS
	if (int result = CreateSDLWindow())
	{
		CloseConsole();
		return result;
	}
	
	if (int result = OpenTimer())
	{
		CleanupSDL();
		CloseConsole();
		return result;
	}
	if (int result = OpenSound())
	{
		CleanupSDL();
		CloseConsole();
		CloseTimer();
		return result;
	}

	WORD tick_count;

#ifdef CHEAT
	rerecordingMode = !playbackMode;
	bool replayFileWriting = false;
#endif

	ClearConsole();
	if (!playbackMode)
	{
#define _ " "
#define S "\x01"
#define i "\x18"
#define TITLE_SCREEN \
		"       ported by David Ellsworth\r\n"\
		"\r\n"\
		_ _ _ _ i i i _ _ i _ _ _ i _ i i i _ i i i i _ _ i i i i i _ _ i i i "\r\n"\
		_ _ _ i S S S i _ S _ _ _ S _ S S S _ S S S S i _ S S S S S _ i S S S i "\r\n"\
		_ _ _ S _ _ _ S _ i i _ _ i _ _ i _ _ i _ _ _ S _ i _ _ _ _ _ S _ _ _ S "\r\n"\
		_ _ _ i _ _ _ _ _ S S _ _ S _ _ S _ _ S _ _ _ i _ S _ _ _ _ _ i "\r\n"\
		_ _ _ S i i i _ _ i _ i _ i _ _ i _ _ i i i i S _ i i i i _ _ S i i i "\r\n"\
		_ _ _ _ S S S i _ S _ S _ S _ _ S _ _ S S S S _ _ S S S S _ _ _ S S S i "\r\n"\
		_ _ _ _ _ _ _ S _ i _ _ i i _ _ i _ _ i _ _ _ _ _ i _ _ _ _ _ _ _ _ _ S "\r\n"\
		_ _ _ i _ _ _ i _ S _ _ S S _ _ S _ _ S _ _ _ _ _ S _ _ _ _ _ i _ _ _ i "\r\n"\
		_ _ _ S i i i S _ i _ _ _ i _ i i i _ i _ _ _ _ _ i i i i i _ S i i i S "\r\n"\
		_ _ _ _ S S S _ _ S _ _ _ S _ S S S _ S _ _ _ _ _ S S S S S _ _ S S S "\r\n"\
		"\r\n\r\n"\
		"(c)Copyright SuperSet Software Corp 1982\r\n"\
		"All Rights Reserved\r\n"\
		"\r\n\r\n"
		WriteTextToConsole(TITLE_SCREEN "Enter skill level (A-Z)(1-9): ");
#undef _
#undef S
#undef i
		// Render the title screen
		RenderFrame();
		ReadSkillLevel();
		if (got_ctrl_break)
			goto do_not_play_again;
	}

	tick_count = GetTickCountWord();
	random_seed_lo = (BYTE)tick_count;
	if (!random_seed_lo)
		random_seed_lo = 444;
	random_seed_hi = tick_count >> 8;
	if (!random_seed_hi)
		random_seed_hi = 555;

	for (;;)
	{
		FILE *replayFile = NULL;
		if (playbackMode)
		{
			replayFile = fopen(replayPlaybackFilename, "rb");
			if (!replayFile)
			{
				fprintf(stderr, "Error opening replay file \"%s\" for playback\n", replayPlaybackFilename);
				goto do_not_play_again;
			}

			#define fread_all(ptr, size, count, stream) \
				do \
				{ \
					size_t _recordsRead = fread(ptr, size, count, stream); \
					if (_recordsRead != count) \
					{ \
						fprintf(stderr, "Error reading from replay file \"%s\" for playback\n", replayPlaybackFilename); \
						goto do_not_play_again; \
					} \
				} while (0)

			fread_all(&random_seed_lo, sizeof(random_seed_lo), 1, replayFile);
			fread_all(&random_seed_hi, sizeof(random_seed_hi), 1, replayFile);
			fread_all(&skillLevelLetter, 1, 1, replayFile);
			fread_all(&skillLevelNumber, 1, 1, replayFile);

			#undef fread_all
		}
		else
		{
			time_t rectime = time(NULL);
			struct tm *rectime_gmt;
			rectime_gmt = gmtime(&rectime);

			char replayFilename[1024];
			sprintf(replayFilename,
					"%04d-%02d-%02d %02d.%02d.%02d.SnipesGame",
					1900+rectime_gmt->tm_year, rectime_gmt->tm_mon+1, rectime_gmt->tm_mday,
					rectime_gmt->tm_hour, rectime_gmt->tm_min, rectime_gmt->tm_sec);

#ifdef CHEAT
			replayFile = fopen(replayFilename, "w+b");
#else
			replayFile = fopen(replayFilename, "wb");
#endif
			if (replayFile)
			{
				setvbuf(replayFile, NULL, _IOFBF, 64);
				fwrite(&random_seed_lo, sizeof(random_seed_lo), 1, replayFile);
				fwrite(&random_seed_hi, sizeof(random_seed_hi), 1, replayFile);
				fwrite(&skillLevelLetter, 1, 1, replayFile);
				fwrite(&skillLevelNumber, 1, 1, replayFile);
			}
		}

		enableElectricWalls           = skillLevelLetter >= 'M'-'A';
		snipePortalsResistSnipeSpears = skillLevelLetter >= 'W'-'A';
		enableBouncingBullets         = bouncingBulletTable           [skillLevelLetter];
		snipeShootingAccuracy         = snipeShootingAccuracyTable    [skillLevelLetter];
		enableSmallSnipes             = enableSmallSnipesTable        [skillLevelLetter];
		smallSnipeExplosionChance     = smallSnipeExplosionChanceTable[skillLevelLetter];
		maxSnipes                     = maxSnipesTable                [skillLevelNumber-1];
		numSnipePortalsAtStart        = numSnipePortalsTable          [skillLevelNumber-1];
		numLives                      = numLivesTable                 [skillLevelNumber-1];
		playerFiringPeriod            = 2;

		OpenDirectConsole();
		if (int result = OpenKeyboard())
		{
			CloseConsole();
			CloseTimer();
			CloseSound();
			return result;
		}

#ifdef CHEAT
		WORD init_random_seed_lo = random_seed_lo;
		WORD init_random_seed_hi = random_seed_hi;

	restart:
#endif
		frame = 0;
		InitializeHUD();
		CreateMaze();
		CreateSnipePortalsAndPlayer();
		SetSoundEffectState(0, SoundEffect_None);

		if (playbackMode && _PLAYBACK_FOR_SCREEN_RECORDING)
		{
			UpdateHUD(false);
			WaitForKeyPress();
			WaitForNextTick(tick_count = GetTickCountWord());
		}

		for (;;)
		{
#ifdef CHEAT
			if (skip_to_frame && frame == skip_to_frame-1)
			{
				skip_to_frame = 0;
				playbackMode = !rerecordingMode;
				auto fileSize = ftell(replayFile);
				if (replayFileWriting)
					changesize(_fileno(replayFile), fileSize);
				BYTE dummy;
				size_t blah = fread(&dummy, 1, 1, replayFile);
				fseek(replayFile, fileSize, SEEK_SET);
			}
			if (UpdateHUD())
				break;
			if (!skip_to_frame)
			{
				DrawViewport();
				if (single_step > 0)
					single_step--;
				while (single_step == 0 && !step_backwards && !(increment_initial_seed && replayFileWriting))
				{
					PollKeyboard();
					if (forfeit_match)
						goto match_ended;
					if (frame == 0)
						step_backwards = 0;
					if (rerecordingMode && !replayFileWriting && replayPlaybackFilename)
					{
						auto fileSize = ftell(replayFile);
						fclose(replayFile);
						replayFile = fopen(replayPlaybackFilename, "r+b");
						if (replayFile)
						{
							replayFileWriting = true;
							playbackMode = false;
						}
						else
						{
							replayFile = fopen(replayPlaybackFilename, "rb");
							if (!replayFile)
							{
								fprintf(stderr, "Error opening replay file \"%s\" for playback\n", replayPlaybackFilename);
								goto do_not_play_again;
							}
							rerecordingMode = false;
							increment_initial_seed = 0;
						}
						fseek(replayFile, fileSize, SEEK_SET);
						changesize(_fileno(replayFile), fileSize);
					}
				}
				if (step_backwards)
				{
					skip_to_frame = frame - step_backwards;
					step_backwards = 0;
					single_step = 0;
					if (skip_to_frame)
						playbackMode = true;
					fflush(replayFile);
					fseek(replayFile, 6, SEEK_SET);

					random_seed_lo = init_random_seed_lo;
					random_seed_hi = init_random_seed_hi;
					currentSoundEffect = SoundEffect_None;

					goto restart;
				}
				if (frame==1 && step_backwards==0 && increment_initial_seed && replayFileWriting)
				{
					init_random_seed_lo += increment_initial_seed;
					increment_initial_seed = 0;
					if (char tmp = init_random_seed_lo >> 8)
					{
						init_random_seed_hi += tmp;
						init_random_seed_lo &= 0xFF;
						init_random_seed_hi &= 0xFF;
					}
					random_seed_lo = init_random_seed_lo;
					random_seed_hi = init_random_seed_hi;

					fflush(replayFile);
					fseek(replayFile, 0, SEEK_SET);
					fwrite(&random_seed_lo, sizeof(random_seed_lo), 1, replayFile);
					fwrite(&random_seed_hi, sizeof(random_seed_hi), 1, replayFile);
					fwrite(&skillLevelLetter, 1, 1, replayFile);
					fwrite(&skillLevelNumber, 1, 1, replayFile);
					goto restart;
				}
			}
#else
			if (UpdateHUD())
				break;
			DrawViewport();
#endif

			if (forfeit_match)
			{
				EraseBottomTwoLines();
				break;
			}

#ifdef CHEAT
			if (!fast_forward && single_step<0 && !skip_to_frame)
#else
			if (!playbackMode || !fast_forward)
#endif
				WaitForNextTick(tick_count);

			UpdateWeapons();
			UpdateSmallSnipes();
			UpdateLargeSnipes();
			UpdateSnipePortals();

			BYTE replayIO;
			bool playbackFinished = false;
			if (playbackMode && fread(&replayIO, 1, 1, replayFile) == 0)
				playbackFinished = true;
			if (UpdatePlayer(playbackMode, replayIO) || playbackFinished)
				break;
			if (!playbackMode && replayFile)
			{
				fwrite(&replayIO, 1, 1, replayFile);
#ifdef CHEAT
				fflush(replayFile);
#endif
			}

			UpdateExplosions();
			UpdateSound();
			
			// Process SDL events and render frame for macOS
			ProcessSDLEvents();
			RenderFrame();
		}
#ifdef CHEAT
	match_ended:
#endif

		if (playbackMode && _PLAYBACK_FOR_SCREEN_RECORDING)
			WaitForNextTick(tick_count);
		ClearSound();
		forfeit_match = false;

		if (replayFile)
			fclose(replayFile);

		CloseDirectConsole(WINDOW_HEIGHT-1 - (playbackMode && !_PLAYBACK_FOR_SCREEN_RECORDING ? 1 : 0));
		if (instant_quit)
			break;
		if (playbackMode && !_PLAYBACK_FOR_SCREEN_RECORDING)
		{
#ifndef _CONSOLE
			// TODO: do this in the console build too, if it's possible to detect when Windows won't itself prompt the user to press any key
			SetConsoleOutputTextColor(0x8);
			WriteTextToConsole("\r\n""Press any key to continue...");
			WaitForKeyPress();
#endif
			break;
		}
		for (;;)
		{
			WriteTextToConsole("Play another game? (Y or N) ");
			if (playbackMode)
			{
				WaitForKeyPress();
				goto do_not_play_again;
			}
			char playAgain;
			auto numread = ReadTextFromConsole(&playAgain, 1);
			if (got_ctrl_break)
				goto do_not_play_again;
			if (!numread)
				continue;
			if (playAgain == 'Y' || playAgain == 'y')
				break;
			if (playAgain == 'N' || playAgain == 'n')
				goto do_not_play_again;
		}
		WriteTextToConsole("Enter new skill level (A-Z)(1-9): ");
		ReadSkillLevel();
		if (got_ctrl_break)
			break;
	}
do_not_play_again:

	CloseSound();
	CloseTimer();
	CleanupSDL();
	CloseConsole();

	return 0;
}
