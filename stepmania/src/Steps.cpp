#include "global.h"
/*
-----------------------------------------------------------------------------
 Class: Steps

 Desc: See header.

 Copyright (c) 2001-2002 by the person(s) listed below.  All rights reserved.
	Chris Danford
	Glenn Maynard
	David Wilson
-----------------------------------------------------------------------------
*/

#include "Steps.h"
#include "song.h"
#include "Steps.h"
#include "IniFile.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "NoteData.h"
#include "GameInput.h"
#include "RageException.h"
#include "MsdFile.h"
#include "GameManager.h"
#include "NoteDataUtil.h"
#include "ProfileManager.h"
#include "PrefsManager.h"

Steps::Steps()
{
	m_StepsType = STEPS_TYPE_INVALID;
	m_LoadedFromProfile = PROFILE_SLOT_INVALID;
	m_uHash = 0;
	m_Difficulty = DIFFICULTY_INVALID;
	m_iMeter = 0;

	notes = NULL;
	notes_comp = NULL;
	parent = NULL;
}

Steps::~Steps()
{
	delete notes;
	delete notes_comp;
}

void Steps::SetNoteData( const NoteData* pNewNoteData )
{
	ASSERT( pNewNoteData->GetNumTracks() == GameManager::NotesTypeToNumTracks(m_StepsType) );

	DeAutogen();

	delete notes;
	notes = new NoteData(*pNewNoteData);
	
	delete notes_comp;
	notes_comp = new CompressedNoteData;
	NoteDataUtil::GetSMNoteDataString( *notes, notes_comp->notes, notes_comp->attacks );
	m_uHash = GetHashForString( notes_comp->notes );
}

void Steps::GetNoteData( NoteData* pNoteDataOut ) const
{
	ASSERT(this);
	ASSERT(pNoteDataOut);

	Decompress();

	if( notes != NULL )
		*pNoteDataOut = *notes;
	else
	{
		pNoteDataOut->ClearAll();
		pNoteDataOut->SetNumTracks( GameManager::NotesTypeToNumTracks(m_StepsType) );
	}
}

void Steps::SetSMNoteData( const CString &notes_comp_, const CString &attacks_comp_ )
{
	delete notes;
	notes = NULL;

	if(!notes_comp)
		notes_comp = new CompressedNoteData;

	notes_comp->notes = notes_comp_;
	notes_comp->attacks = attacks_comp_;
	m_uHash = GetHashForString( notes_comp->notes );
}

void Steps::GetSMNoteData( CString &notes_comp_out, CString &attacks_comp_out ) const
{
	if(!notes_comp)
	{
		if(!notes) 
		{
			/* no data is no data */
			notes_comp_out = attacks_comp_out = "";
			return;
		}

		notes_comp = new CompressedNoteData;
		NoteDataUtil::GetSMNoteDataString( *notes, notes_comp->notes, notes_comp->attacks );
	}

	notes_comp_out = notes_comp->notes;
	attacks_comp_out = notes_comp->attacks;
}

float Steps::PredictMeter() const
{
	float pMeter = 0.775f;
	
	const float RadarCoeffs[NUM_RADAR_CATEGORIES] =
	{
		10.1f, 5.27f,-0.905f, -1.10f, 2.86f,
		0,0,0,0,0
	};
	for( int r = 0; r < NUM_RADAR_CATEGORIES; ++r )
		pMeter += this->GetRadarValues()[r] * RadarCoeffs[r];
	
	const float DifficultyCoeffs[NUM_DIFFICULTIES] =
	{
		-0.877f, -0.877f, 0, 0.722f, 0.722f, 0
	};
	pMeter += DifficultyCoeffs[this->GetDifficulty()];
	
	// Init non-radar values
	const float SV = this->GetRadarValues()[RADAR_STREAM] * this->GetRadarValues()[RADAR_VOLTAGE];
	const float ChaosSquare = this->GetRadarValues()[RADAR_CHAOS] * this->GetRadarValues()[RADAR_CHAOS];
	pMeter += -6.35f * SV;
	pMeter += -2.58f * ChaosSquare;
	if (pMeter < 1) pMeter = 1;	
	return pMeter;
}


void Steps::TidyUpData()
{
	if( GetDifficulty() == DIFFICULTY_INVALID )
		SetDifficulty( StringToDifficulty(GetDescription()) );
	
	if( GetDifficulty() == DIFFICULTY_INVALID )
	{
		if(		 GetMeter() == 1 )	SetDifficulty( DIFFICULTY_BEGINNER );
		else if( GetMeter() <= 3 )	SetDifficulty( DIFFICULTY_EASY );
		else if( GetMeter() <= 6 )	SetDifficulty( DIFFICULTY_MEDIUM );
		else						SetDifficulty( DIFFICULTY_HARD );
	}

	if( GetMeter() < 1) // meter is invalid
	{
		// Why not just use PredictMeter()
		SetMeter( int(PredictMeter()) );
		/*
		// guess meter from difficulty class
		switch( GetDifficulty() )
		{
		case DIFFICULTY_BEGINNER:	SetMeter(1);	break;
		case DIFFICULTY_EASY:		SetMeter(3);	break;
		case DIFFICULTY_MEDIUM:		SetMeter(5);	break;
		case DIFFICULTY_HARD:		SetMeter(8);	break;
		case DIFFICULTY_CHALLENGE:	SetMeter(8);	break;
		case DIFFICULTY_INVALID:	SetMeter(5);	break;
		default:	ASSERT(0);
		}*/
	}

	// Don't put garbage in the desciption.
//	if( m_sDescription.empty() )
//		m_sDescription = Capitalize( DifficultyToString(m_Difficulty) );
}

void Steps::Decompress() const
{
	if(notes)
	{
		return;	// already decompressed
	}
	else if(parent)
	{
		// get autogen notes
		NoteData pdata;
		parent->GetNoteData(&pdata);

		notes = new NoteData;

		int iNewTracks = GameManager::NotesTypeToNumTracks(m_StepsType);

		notes->LoadTransformedSlidingWindow( &pdata, iNewTracks );

		NoteDataUtil::FixImpossibleRows( *notes, m_StepsType );
	}
	else if(!notes_comp)
	{
		/* there is no data, do nothing */
	}
	else
	{
		// load from compressed
		notes = new NoteData;
		notes->SetNumTracks( GameManager::NotesTypeToNumTracks(m_StepsType) );

		NoteDataUtil::LoadFromSMNoteDataString(*notes, notes_comp->notes, notes_comp->attacks );
	}
}

void Steps::Compress() const
{
	if(!notes_comp)
	{
		if(!notes) return; /* no data is no data */
		notes_comp = new CompressedNoteData;
		NoteDataUtil::GetSMNoteDataString( *notes, notes_comp->notes, notes_comp->attacks );
	}

	delete notes;
	notes = NULL;
}

/* Copy our parent's data.  This is done when we're being changed from autogen
 * to normal. (needed?) */
void Steps::DeAutogen()
{
	if(!parent)
		return; /* OK */

	Decompress();	// fills in notes with sliding window transform

	m_sDescription	= Real()->m_sDescription;
	m_Difficulty	= Real()->m_Difficulty;
	m_iMeter		= Real()->m_iMeter;
	m_RadarValues   = Real()->m_RadarValues;

	parent = NULL;

	Compress();
}

void Steps::AutogenFrom( const Steps *parent_, StepsType ntTo )
{
	parent = parent_;
	m_StepsType = ntTo;
}

void Steps::CopyFrom( Steps* pSource, StepsType ntTo )	// pSource does not have to be of the same StepsType!
{
	m_StepsType = ntTo;
	NoteData noteData;
	pSource->GetNoteData( &noteData );
	noteData.SetNumTracks( GameManager::NotesTypeToNumTracks(ntTo) ); 
	this->SetNoteData( &noteData );
	this->SetDescription( "Copied from "+pSource->GetDescription() );
	this->SetDifficulty( pSource->GetDifficulty() );
	this->SetMeter( pSource->GetMeter() );

	const float* radarValues = pSource->GetRadarValues();
	for( int r=0; r<NUM_RADAR_CATEGORIES; r++ )
		this->SetRadarValue( (RadarCategory)r, radarValues[r] );
}

void Steps::CreateBlank( StepsType ntTo )
{
	m_StepsType = ntTo;
	NoteData noteData;
	noteData.SetNumTracks( GameManager::NotesTypeToNumTracks(ntTo) );
	this->SetNoteData( &noteData );
}


const Steps *Steps::Real() const
{
	ASSERT( this );
	if(parent) return parent;
	return this;
}

bool Steps::IsAutogen() const
{
	return parent != NULL;
}

void Steps::SetDescription(CString desc)
{
	DeAutogen();
	m_sDescription = desc;
}

void Steps::SetDifficulty(Difficulty d)
{
	DeAutogen();
	m_Difficulty = d;
}

void Steps::SetMeter(int meter)
{
	DeAutogen();
	m_iMeter = meter;
}

void Steps::SetRadarValue(int r, float val)
{
	DeAutogen();
	ASSERT(r < NUM_RADAR_CATEGORIES);
	m_RadarValues[r] = val;
}


