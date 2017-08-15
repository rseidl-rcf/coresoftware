/*===============================================================*
 *                       March 19th 2017                         *
    mRICH Stepping Action created by Cheuk-Ping Wong @GSU        *
 *===============================================================*/
#include "PHG4mRICHSteppingAction.h"
#include "PHG4mRICHDetector.h"
#include "PHG4Parameters.h"
#include "PHG4StepStatusDecode.h"

#include <g4main/PHG4HitContainer.h>
#include <g4main/PHG4Hit.h>
#include <g4main/PHG4Hitv1.h>
#include <g4main/PHG4Shower.h>
#include <g4main/PHG4TrackUserInfoV1.h>

#include <TSystem.h>

#include <phool/getClass.h>

#include <Geant4/G4Step.hh>
#include <Geant4/G4MaterialCutsCouple.hh>
#include <Geant4/G4SystemOfUnits.hh>

#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
// this is an ugly hack, the gcc optimizer has a bug which
// triggers the uninitialized variable warning which
// stops compilation because of our -Werror
#include <boost/version.hpp> // to get BOOST_VERSION
#if (__GNUC__ == 4 && __GNUC_MINOR__ == 4 && BOOST_VERSION == 105700 )
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma message "ignoring bogus gcc warning in boost header lexical_cast.hpp"
#include <boost/lexical_cast.hpp>
#pragma GCC diagnostic warning "-Wuninitialized"
#else
#include <boost/lexical_cast.hpp>
#endif

#include <iostream>

using namespace std;
using namespace CLHEP;

//____________________________________________________________________________..
PHG4mRICHSteppingAction::PHG4mRICHSteppingAction( PHG4mRICHDetector* detector,PHG4Parameters* params):
  detector_( detector ),
  active(params->get_int_param("active")),
  IsBlackHole(params->get_int_param("blackhole")),
  use_g4_steps(params->get_int_param("use_g4steps")),
  detectorname(params->get_string_param("detectorname")),
  superdetector(params->get_string_param("superdetector")),
  hits_(NULL),
  absorberhits_(NULL),
  hit(NULL)
{
}
//____________________________________________________________________________..
PHG4mRICHSteppingAction::~PHG4mRICHSteppingAction()
{
  delete hit;
}
//____________________________________________________________________________..
bool PHG4mRICHSteppingAction::UserSteppingAction( const G4Step* aStep, bool )
{
  ////int savetrackid;
  PHG4HitContainer *savehitcontainer=nullptr;
  PHG4Shower *saveshower=nullptr;

  G4TouchableHandle touch = aStep->GetPreStepPoint()->GetTouchableHandle();
  G4VPhysicalVolume* volume = touch->GetVolume();

  bool whichactive = detector_->IsInmRICH(volume);
  if ( !whichactive  ) return false;

  int module_id=GetModuleID(volume);
  if (!module_id) { 
    cout<<"ERROR: module_id<0"<<endl; 
    return false;
  }

  //-----------------------------------------------------------------------------------//
  /* Get energy deposited by this step */
  G4double edep = aStep->GetTotalEnergyDeposit() / GeV;
  G4double eion = (aStep->GetTotalEnergyDeposit() - aStep->GetNonIonizingEnergyDeposit()) / GeV;

  /* Get pointer to associated Geant4 track */
  const G4Track* aTrack = aStep->GetTrack();
  //int PID=aTrack->GetDefinition()->GetPDGEncoding();
  //if (PID==0) cout<<":::::::::::::::::: PID="<<PID<<" ::::::::::::::::::"<<endl;
  //-----------------------------------------------------------------------------------//
  // if this block stops everything, just put all kinetic energy into edep
  if (IsBlackHole) {
    edep = aTrack->GetKineticEnergy() / GeV;
    G4Track* killtrack = const_cast<G4Track *> (aTrack);
    killtrack->SetTrackStatus(fStopAndKill);
  }

  //-----------------------------------------------------------------------------------//
  /* Make sure we are in a volume */
  if ( active ) {
    /* Check if particle is 'geantino' */
    bool geantino = false;
    if (aTrack->GetParticleDefinition()->GetPDGEncoding() == 0 &&
	aTrack->GetParticleDefinition()->GetParticleName().find("geantino") != string::npos) {
      geantino = true;
    }

    /* Get Geant4 pre- and post-step points */
    G4StepPoint * prePoint = aStep->GetPreStepPoint();
    G4StepPoint * postPoint = aStep->GetPostStepPoint();

    switch (prePoint->GetStepStatus()) {
      //-----------------
      case fGeomBoundary:
      //-----------------
      case fUndefined:
	if (! hit) hit = new PHG4Hitv1();
	/* Set hit location (space point) */
	hit->set_x( 0, prePoint->GetPosition().x() / cm);
	hit->set_y( 0, prePoint->GetPosition().y() / cm );
	hit->set_z( 0, prePoint->GetPosition().z() / cm );
	
	/* Set hit time */
	hit->set_t( 0, prePoint->GetGlobalTime() / nanosecond );
	
	//set the track ID
	hit->set_trkid(aTrack->GetTrackID());
	//savetrackid = aTrack->GetTrackID();

	/* set intial energy deposit */
	hit->set_edep( 0 );
	if (whichactive) {
	  hit->set_eion( 0 );
	  //hit->set_light_yield(0);
	  savehitcontainer = hits_;
	}
	else savehitcontainer = absorberhits_;
	
	// here we set what is common for scintillator and absorber hits
	if ( G4VUserTrackInformation* p = aTrack->GetUserInformation() ) {
	  if ( PHG4TrackUserInfoV1* pp = dynamic_cast<PHG4TrackUserInfoV1*>(p) ) {
	    hit->set_trkid(pp->GetUserTrackId());
	    hit->set_shower_id(pp->GetShower()->get_id());
	    saveshower = pp->GetShower();
	  }
	}
	break;
	//-----------------
    default:
	break;
    }

    //-----------------------------------------------------------------------------------//
    /* Update exit values- will be overwritten with every step until
     * we leave the volume or the particle ceases to exist */
    hit->set_x( 1, postPoint->GetPosition().x() / cm );
    hit->set_y( 1, postPoint->GetPosition().y() / cm );
    hit->set_z( 1, postPoint->GetPosition().z() / cm );
    
    hit->set_t( 1, postPoint->GetGlobalTime() / nanosecond );
    
    /* sum up the energy to get total deposited */
    hit->set_edep(hit->get_edep() + edep);
    hit->set_eion(hit->get_eion() + eion);
    
    if (geantino) {
      hit->set_edep(-1); // only energy=0 g4hits get dropped, this way geantinos survive the g4hit compression
      hit->set_eion(-1);
    }
    //if (edep!=0 || PID==0) {
    if (edep > 0 /*&& (whichactive > 0 || absorbertruth > 0)*/) {
      if ( G4VUserTrackInformation* p = aTrack->GetUserInformation() ) {
	if ( PHG4TrackUserInfoV1* pp = dynamic_cast<PHG4TrackUserInfoV1*>(p) ) {
	  pp->SetKeep(1); // we want to keep the track
	}
      }
    }

    //-----------------------------------------------------------------------------------//
    // if any of these conditions is true this is the last step in
    // this volume and we need to save the hit
    if (postPoint->GetStepStatus() == fGeomBoundary ||
        postPoint->GetStepStatus() == fWorldBoundary ||
        postPoint->GetStepStatus() == fAtRestDoItProc ||
        aTrack->GetTrackStatus() == fStopAndKill) {
      // save only hits with energy deposit (or -1 for geantino)
      if (hit->get_edep()){
	savehitcontainer->AddHit(module_id, hit);
	if (saveshower) saveshower->add_g4hit_id(hits_->GetID(), hit->get_hit_id());
	// ownership has been transferred to container, set to null
	// so we will create a new hit for the next track
	hit = nullptr;
      }
      else {
	// if this hit has no energy deposit, just reset it for reuse
	// this means we have to delete it in the dtor. If this was
	// the last hit we processed the memory is still allocated
	hit->Reset();
      }
    }

    return true;
    
  }
  //else return false;    //this is not safe. It should always return something.

  return false;
}


//____________________________________________________________________________..
void PHG4mRICHSteppingAction::SetInterfacePointers( PHCompositeNode* topNode )
{

  string hitnodename;
  string absorbernodename;

  if (superdetector !="NONE") {
    hitnodename = "G4HIT_" + superdetector;
    absorbernodename =  "G4HIT_ABSORBER_" + superdetector;
  }
  else {
    hitnodename = "G4HIT_" + detectorname;
    absorbernodename =  "G4HIT_ABSORBER_" + detectorname;
  }
  
  //now look for the map and grab a pointer to it.
  hits_ =  findNode::getClass<PHG4HitContainer>( topNode , hitnodename.c_str() );
  absorberhits_ =  findNode::getClass<PHG4HitContainer>( topNode , absorbernodename.c_str() );
  
  // if we do not find the node it's messed up.
  if ( ! hits_ ) {
    std::cout << "PHG4mRICHSteppingAction::SetTopNode - unable to find " << hitnodename << std::endl;
  }
  if ( ! absorberhits_) {
    if (verbosity > 0) {
      cout << "PHG4mRICHSteppingAction::SetTopNode - unable to find " << absorbernodename << endl;
    }
  }
}
//____________________________________________________________________________..
int PHG4mRICHSteppingAction::GetModuleID(G4VPhysicalVolume* volume)
{
  // G4AssemblyVolumes naming convention:
  //     av_WWW_impr_XXX_YYY_ZZZ
  // where:
  //     WWW - assembly volume instance number
  //     XXX - assembly volume imprint number 
  //     YYY - the name of the placed logical volume
  //     ZZZ - the logical volume index inside the assembly volume
  // e.g. av_1_impr_82_HcalInnerScinti_11_pv_11 
  // 82 the number of the scintillator mother volume
  // HcalInnerScinti_11: name of scintillator slat  
  // 11: number of scintillator slat logical volume 
  // use boost tokenizer to separate the _, then take value
  // after "impr" for mother volume and after "pv" for scintillator slat
  // use boost lexical cast for string -> int conversion 
  int module_id = -1;

  boost::char_separator<char> sep("_");
  boost::tokenizer<boost::char_separator<char>> tok(volume->GetName(), sep);
  boost::tokenizer<boost::char_separator<char>>::const_iterator tokeniter;

  for (tokeniter = tok.begin(); tokeniter != tok.end(); ++tokeniter) {
    if (*tokeniter == "pv") {
      ++tokeniter;
      if (tokeniter != tok.end()) {
	module_id = boost::lexical_cast<int>(*tokeniter);
	//if (module_id<0) cout<<"ERROR: module_id<0"<<endl;
      }
      else {
	cout << PHWHERE << " Error parsing " << volume->GetName()
	     << " for mother scinti slat id " << endl;
	gSystem->Exit(1);
      }
    }
  }
  return module_id;
}
