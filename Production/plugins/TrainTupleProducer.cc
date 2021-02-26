/*! Creates tuple for tau analysis.
*/

#include "Compression.h"

#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ServiceRegistry/interface/Service.h"

#include "SimDataFormats/GeneratorProducts/interface/GenEventInfoProduct.h"
#include "AnalysisDataFormats/TopObjects/interface/TtGenEvent.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include "CommonTools/Utils/interface/StringToEnumValue.h"
#include "RecoTauTag/RecoTau/interface/PFRecoTauClusterVariables.h"

#include "TauMLTools/Core/interface/Tools.h"
#include "TauMLTools/Core/interface/TextIO.h"
#include "TauMLTools/Analysis/interface/TrainTuple.h"
#include "TauMLTools/Analysis/interface/SummaryTuple.h"
#include "TauMLTools/Analysis/interface/TauIdResults.h"
#include "TauMLTools/Production/interface/GenTruthTools.h"
#include "TauMLTools/Production/interface/TauAnalysis.h"
#include "TauMLTools/Production/interface/MuonHitMatch.h"
#include "TauMLTools/Production/interface/TauJet.h"
#include "TauMLTools/Analysis/interface/GenLepton.h"

#include "DataFormats/L1Trigger/interface/Tau.h"
#include "DataFormats/TauReco/interface/PFTau.h"
#include "DataFormats/TauReco/interface/PFTauDiscriminator.h"
#include "DataFormats/Candidate/interface/LeafCandidate.h"
#include "DataFormats/RecoCandidate/interface/RecoEcalCandidate.h"
#include "DataFormats/TrackReco/interface/HitPattern.h"
#include "DataFormats/MuonReco/interface/MuonSelectors.h"
#include "DataFormats/TauReco/interface/PFTauTransverseImpactParameterAssociation.h"
#include "DataFormats/TauReco/interface/PFTauTransverseImpactParameterFwd.h"
#include "DataFormats/TauReco/interface/PFTauTransverseImpactParameter.h"
#include "DataFormats/Common/interface/AssociationVector.h"
#include "DataFormats/Common/interface/Association.h"
#include "DataFormats/CaloTowers/interface/CaloTowerCollection.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/JetReco/interface/CaloJetCollection.h"
#include "DataFormats/CaloRecHit/interface/CaloRecHit.h"
#include "DataFormats/HcalDetId/interface/HcalDetId.h"
#include "Geometry/CaloGeometry/interface/CaloGeometry.h"
#include "Geometry/CaloTopology/interface/HcalTopology.h"
#include "DataFormats/HcalRecHit/interface/HBHERecHit.h"
#include "DataFormats/HcalRecHit/interface/HFRecHit.h"
#include "DataFormats/HcalRecHit/interface/HORecHit.h"
#include "DataFormats/HcalRecHit/interface/HcalRecHitDefs.h"
#include "DataFormats/EcalRecHit/interface/EcalRecHit.h"
#include "Geometry/CaloTopology/interface/CaloTowerTopology.h"
#include "Geometry/CaloTopology/interface/CaloTowerConstituentsMap.h"
#include "Geometry/CaloGeometry/interface/CaloCellGeometry.h"
#include "Geometry/CaloGeometry/interface/CaloSubdetectorGeometry.h"
#include "RecoLocalCalo/CaloTowersCreator/src/CaloTowersCreator.h"
#include "Geometry/CaloGeometry/interface/CaloGeometry.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "Geometry/CaloTopology/interface/HcalTopology.h"
#include "Geometry/CaloTopology/interface/CaloTowerTopology.h"
#include "RecoLocalCalo/CaloTowersCreator/interface/EScales.h"
#include "Geometry/Records/interface/CaloGeometryRecord.h"
// severity level for ECAL
#include "RecoLocalCalo/EcalRecAlgos/interface/EcalSeverityLevelAlgoRcd.h"
#include "DataFormats/HLTReco/interface/TriggerFilterObjectWithRefs.h"

namespace tau_analysis {

struct TrainTupleProducerData {
    using clock = std::chrono::system_clock;

    const clock::time_point start;
    train_tuple::TrainTuple trainTuple;
    tau_tuple::SummaryTuple summaryTuple;
    std::mutex mutex;

private:
    size_t n_producers;

    TrainTupleProducerData(TFile& file) :
        start(clock::now()),
        trainTuple("taus", &file, false),
        summaryTuple("summary", &file, false),
        n_producers(0)
    {
        summaryTuple().numberOfProcessedEvents = 0;
    }

    ~TrainTupleProducerData() {}

public:

    static TrainTupleProducerData* RequestGlobalData()
    {
        TrainTupleProducerData* data = GetGlobalData();
        if(data == nullptr)
            throw cms::Exception("TrainTupleProducerData") << "Request after all data copies were released.";
        {
            std::lock_guard<std::mutex> lock(data->mutex);
            ++data->n_producers;
            std::cout << "New request of TrainTupleProducerData. Total number of producers = " << data->n_producers
                      << "." << std::endl;
        }
        return data;
    }

    static void ReleaseGlobalData()
    {
        TrainTupleProducerData*& data = GetGlobalData();
        if(data == nullptr)
            throw cms::Exception("TrainTupleProducerData") << "Another release after all data copies were released.";
        {
            std::lock_guard<std::mutex> lock(data->mutex);
            if(!data->n_producers)
                throw cms::Exception("TrainTupleProducerData") << "Release before any request.";
            --data->n_producers;
            std::cout << "TrainTupleProducerData has been released. Total number of producers = " << data->n_producers
                      << "." << std::endl;
            if(!data->n_producers) {
                data->trainTuple.Write();
                const auto stop = clock::now();
                data->summaryTuple().exeTime = static_cast<unsigned>(
                            std::chrono::duration_cast<std::chrono::seconds>(stop - data->start).count());
                data->summaryTuple.Fill();
                data->summaryTuple.Write();
                delete data;
                data = nullptr;
                std::cout << "TrainTupleProducerData has been destroyed." << std::endl;
            }
        }

    }

private:
    static TrainTupleProducerData*& GetGlobalData()
    {
        static TrainTupleProducerData* data = InitializeGlobalData();
        return data;
    }

    static TrainTupleProducerData* InitializeGlobalData()
    {
        TFile& file = edm::Service<TFileService>()->file();
        file.SetCompressionAlgorithm(ROOT::kZLIB);
        file.SetCompressionLevel(9);
        TrainTupleProducerData* data = new TrainTupleProducerData(file);
        std::cout << "TrainTupleProducerData has been created." << std::endl;
        return data;
    }
};

class TrainTupleProducer : public edm::EDAnalyzer {
public:
    using TauDiscriminator = reco::PFTauDiscriminator;
    struct caloRecHitCollections{
      const HBHERecHitCollection  *hbhe;
      const HORecHitCollection *ho;
      const HFRecHitCollection *hf;
      const EcalRecHitCollection *eb;
      const EcalRecHitCollection *ee;
      const CaloGeometry *Geometry;
    };
    TrainTupleProducer(const edm::ParameterSet& cfg) :
        isMC(cfg.getParameter<bool>("isMC")),
        genEvent_token(mayConsume<GenEventInfoProduct>(cfg.getParameter<edm::InputTag>("genEvent"))),
        genParticles_token(mayConsume<std::vector<reco::GenParticle>>(cfg.getParameter<edm::InputTag>("genParticles"))),
        puInfo_token(mayConsume<std::vector<PileupSummaryInfo>>(cfg.getParameter<edm::InputTag>("puInfo"))),
        l1Taus_token(consumes<l1t::TauBxCollection>(cfg.getParameter<edm::InputTag>("l1taus"))), // --> VBor
        caloTowers_token(consumes<CaloTowerCollection>(cfg.getParameter<edm::InputTag>("caloTowers"))),
        caloTaus_token(consumes<reco::CaloJetCollection>(cfg.getParameter<edm::InputTag>("caloTaus"))),
        hbhe_token(consumes<HBHERecHitCollection>(cfg.getParameter<edm::InputTag>("hbheInput"))),
        ho_token(consumes<HORecHitCollection>(cfg.getParameter<edm::InputTag>("hoInput"))),
        hf_token( consumes<HFRecHitCollection>(cfg.getParameter<edm::InputTag>("hfInput"))),
        ecalLabels(cfg.getParameter<std::vector<edm::InputTag> >("ecalInputs")),
        Geometry_token(esConsumes<CaloGeometry,CaloGeometryRecord>()),
        vertices_token(consumes<reco::VertexCollection>(cfg.getParameter<edm::InputTag>("vertices"))),
        pataVertices_token(consumes<reco::VertexCollection>(cfg.getParameter<edm::InputTag>("pataVertices"))),
        Tracks_token(consumes<reco::TrackCollection>(cfg.getParameter<edm::InputTag>("Tracks"))),
        pataTracks_token(consumes<reco::TrackCollection>(cfg.getParameter<edm::InputTag>("pataTracks"))),
        data(TrainTupleProducerData::RequestGlobalData()),
        trainTuple(data->trainTuple),
        summaryTuple(data->summaryTuple)
    {
      const auto& pSet = cfg.getParameterSet("l1Results");
      for (const auto& l1name : pSet.getParameterNames()){
          l1_token_map[l1name] = consumes<trigger::TriggerFilterObjectWithRefs>(pSet.getParameter<edm::InputTag>(l1name));
      }
      const unsigned nLabels = ecalLabels.size();
      for (unsigned i = 0; i != nLabels; i++)
        ecal_tokens.push_back(consumes<EcalRecHitCollection>(ecalLabels[i]));

    }
private:
    static constexpr float default_value = train_tuple::DefaultFillValue<float>();
    static constexpr int default_int_value = train_tuple::DefaultFillValue<int>();
    static constexpr int default_unsigned_value = train_tuple::DefaultFillValue<unsigned>();

    virtual void analyze(const edm::Event& event, const edm::EventSetup& eventsetup) override
    {
        std::lock_guard<std::mutex> lock(data->mutex);
        summaryTuple().numberOfProcessedEvents++;

        trainTuple().run  = event.id().run();
        trainTuple().lumi = event.id().luminosityBlock();
        trainTuple().evt  = event.id().event();

        edm::Handle<reco::VertexCollection> vertices;
        event.getByToken(vertices_token, vertices);

        edm::Handle<reco::VertexCollection> pataVertices;
        event.getByToken(pataVertices_token, pataVertices);

        if(isMC) {
            edm::Handle<GenEventInfoProduct> genEvent;
            event.getByToken(genEvent_token, genEvent);
            trainTuple().genEventWeight = static_cast<float>(genEvent->weight());

            edm::Handle<std::vector<PileupSummaryInfo>> puInfo;
            event.getByToken(puInfo_token, puInfo);
            trainTuple().npu = gen_truth::GetNumberOfPileUpInteractions(puInfo);
        }

        edm::Handle<l1t::TauBxCollection> l1Taus;
        event.getByToken(l1Taus_token, l1Taus);

        std::map<std::string, edm::Handle<trigger::TriggerFilterObjectWithRefs>> l1_handle_map;
        for(const auto& element : l1_token_map){
          event.getByToken(element.second, l1_handle_map[element.first]);
        }

        edm::Handle<EcalRecHitCollection> ebHandle;
        edm::Handle<EcalRecHitCollection> eeHandle;
        for (std::vector<edm::EDGetTokenT<EcalRecHitCollection> >::const_iterator i = ecal_tokens.begin(); i != ecal_tokens.end(); i++) {
          edm::Handle<EcalRecHitCollection> ec_tmp;
          event.getByToken(*i, ec_tmp);
          if (ec_tmp->empty())
            continue;
          // check if this is EB or EE
          if ((ec_tmp->begin()->detid()).subdetId() == EcalBarrel) {
            ebHandle = ec_tmp;
          }
          else if ((ec_tmp->begin()->detid()).subdetId() == EcalEndcap) {
            eeHandle = ec_tmp;
          }
        }
        std::vector<edm::EDGetTokenT<EcalRecHitCollection> >::const_iterator i;
        for (i = ecal_tokens.begin(); i != ecal_tokens.end(); i++) {
          edm::Handle<EcalRecHitCollection> ec;
          event.getByToken(*i, ec);
        }
        edm::Handle<HBHERecHitCollection> hbhe;
        event.getByToken(hbhe_token, hbhe);

        edm::Handle<HORecHitCollection> ho;
        event.getByToken(ho_token, ho);

        edm::Handle<HFRecHitCollection> hf;
        event.getByToken(hf_token, hf);

        edm::Handle<CaloTowerCollection> caloTowers;
        event.getByToken(caloTowers_token, caloTowers);

        edm::Handle<reco::TrackCollection> Tracks;
        event.getByToken(Tracks_token, Tracks);

        edm::Handle<reco::TrackCollection> pataTracks;
        event.getByToken(pataTracks_token, pataTracks);

        edm::Handle<reco::CaloJetCollection> caloTaus;
        event.getByToken(caloTaus_token, caloTaus);
        pat::JetCollection jets;

        edm::ESHandle<CaloGeometry> Geometry = eventsetup.getHandle(Geometry_token);

        caloRecHitCollections AllCaloRecHits;
        AllCaloRecHits.hbhe= &*hbhe;
        AllCaloRecHits.ho= &*ho;
        AllCaloRecHits.hf= &*hf;
        AllCaloRecHits.eb= &*ebHandle;
        AllCaloRecHits.ee= &*eeHandle;
        AllCaloRecHits.Geometry = &*Geometry;

        edm::Handle<std::vector<reco::GenParticle>> hGenParticles;
        if(isMC){
            event.getByToken(genParticles_token, hGenParticles);
            static std::vector<reco_tau::gen_truth::GenLepton> GenLeptons = reco_tau::gen_truth::GenLepton::fromGenParticleCollection(*hGenParticles);
            FillGenLepton(GenLeptons);
          }

        //check inputs
        FillL1Objects(*l1Taus, l1_handle_map);
        FillTracks(*Tracks, "track", *vertices);
        FillTracks(*pataTracks, "patatrack", *pataVertices);
        FillVertices(*vertices, "vert");
        FillVertices(*pataVertices, "patavert");
        FillCaloRecHit(AllCaloRecHits);
        FillCaloTowers(*caloTowers);
        FillCaloTaus(*caloTaus);

        trainTuple.Fill();
    }

    virtual void endJob() override
    {
        TrainTupleProducerData::ReleaseGlobalData();
    }

private:


  void FillGenLepton(const std::vector<reco_tau::gen_truth::GenLepton>& genLeptons)
  {
      for(const auto& genLepton : genLeptons){
        trainTuple().genLepton_nParticles.push_back(static_cast<int>(genLepton.allParticles().size()));
        trainTuple().genLepton_kind.push_back(static_cast<int>(genLepton.kind()));
        trainTuple().genLepton_charge.push_back(genLepton.charge());
        trainTuple().genLepton_vis_pt.push_back(static_cast<float>(genLepton.visibleP4().pt()));
        trainTuple().genLepton_vis_eta.push_back(static_cast<float>(genLepton.visibleP4().eta()));
        trainTuple().genLepton_vis_phi.push_back(static_cast<float>(genLepton.visibleP4().phi()));
        trainTuple().genLepton_vis_mass.push_back(static_cast<float>(genLepton.visibleP4().mass()));
        //trainTuple().genLepton_lastMotherIndex default_int_value;

        const auto ref_ptr = genLepton.allParticles().data();

        auto getIndex = [&](const reco_tau::gen_truth::GenParticle* particle) {
              int pos = -1;
              if(particle) {
                  pos = static_cast<int>(particle - ref_ptr);
                  if(pos < 0 || pos >= static_cast<int>(genLepton.allParticles().size()))
                      throw cms::Exception("TrainTupleProducer") << "Unable to determine a gen particle index.";
              }
              return pos;
          };

          auto encodeMotherIndex = [&](const std::set<const reco_tau::gen_truth::GenParticle*>& mothers) {
              static constexpr int shift_scale = 1000;

              if(mothers.empty()) return -1;
              if(mothers.size() > 2)
                  throw cms::Exception("TrainTupleProducer") << "Gen particle with >= 2 mothers.";
              if(mothers.size() > 1 && genLepton.allParticles().size() > static_cast<size_t>(shift_scale))
                  throw cms::Exception("TrainTupleProducer") << "Too many gen particles per gen lepton.";
              int pos = 0;
              int shift = 1;
              for(auto mother : mothers) {
                  pos = pos + shift * getIndex(mother);
                  shift *= shift_scale;
              }
              return pos;
          };

          trainTuple().genLepton_lastMotherIndex.push_back(static_cast<int>(genLepton.mothers().size()) - 1);
          for(const auto& p : genLepton.allParticles()) {
              trainTuple().genParticle_pdgId.push_back(p.pdgId);
              trainTuple().genParticle_mother.push_back(encodeMotherIndex(p.mothers));
              trainTuple().genParticle_charge.push_back(p.charge);
              trainTuple().genParticle_isFirstCopy.push_back(p.isFirstCopy);
              trainTuple().genParticle_isLastCopy.push_back(p.isLastCopy);
              trainTuple().genParticle_pt.push_back(p.p4.pt());
              trainTuple().genParticle_eta.push_back(p.p4.eta());
              trainTuple().genParticle_phi.push_back(p.p4.phi());
              trainTuple().genParticle_mass.push_back(p.p4.mass());
              trainTuple().genParticle_vtx_x.push_back(p.vertex.x());
              trainTuple().genParticle_vtx_y.push_back(p.vertex.y());
              trainTuple().genParticle_vtx_z.push_back(p.vertex.z());
            }
          }
    }


    void FillL1Objects(const l1t::TauBxCollection& l1Taus, const std::map<std::string, edm::Handle<trigger::TriggerFilterObjectWithRefs>>& l1_handle_map)
    {
        /* passa anche la mappa di l1Results -> per ogni oggetto salva come booleani*/
        std::map<std::string, bool> boolean_map;
        for(auto iter = l1Taus.begin(0); iter != l1Taus.end(0); ++iter) {
          const l1t::Tau * pointer_to_l1tau = &* iter;
          for(const auto& element : l1_handle_map){
            const trigger::TriggerFilterObjectWithRefs object = *element.second;
            const std::vector<edm::Ref<BXVector<l1t::Tau>>> tau_vector = object.l1ttauRefs();
            bool comparison = false;
            for (const auto &k : tau_vector ){
              const l1t::Tau * pointer_to_l1tau_in_map = &*k;
              if(pointer_to_l1tau == pointer_to_l1tau_in_map){
                  comparison = true ;
                  break;
              }
            }
            boolean_map[element.first]=comparison;
          }

           /*punto anche all'altro oggetto nella mappa (loop su mappa!)  e vedere se diventano dello stesso tipo
          poi confrontarli e restituire un booleano */
            trainTuple().l1Tau_pt.push_back(static_cast<float>(iter->polarP4().pt()));
            trainTuple().l1Tau_eta.push_back(static_cast<float>(iter->polarP4().eta()));
            trainTuple().l1Tau_phi.push_back(static_cast<float>(iter->polarP4().phi()));
            trainTuple().l1Tau_mass.push_back(static_cast<float>(iter->polarP4().mass()));

            trainTuple().l1Tau_towerIEta.push_back(static_cast<short int>(iter->towerIEta()));
            trainTuple().l1Tau_towerIPhi.push_back(static_cast<short int>(iter->towerIPhi()));
            trainTuple().l1Tau_rawEt.push_back(static_cast<short int>(iter->rawEt()));
            trainTuple().l1Tau_isoEt.push_back(static_cast<short int>(iter->isoEt()));
            trainTuple().l1Tau_hasEM.push_back(static_cast<bool>(iter->hasEM()));
            trainTuple().l1Tau_isMerged.push_back(static_cast<bool>(iter->isMerged()));

            trainTuple().l1Tau_hwIso.push_back(iter->hwIso());
            trainTuple().l1Tau_hwQual.push_back(iter->hwQual());
            trainTuple().L1_LooseIsoEG22er2p1_IsoTau26er2p1_dR_Min0p3.push_back(boolean_map.at("L1_LooseIsoEG22er2p1_IsoTau26er2p1_dR_Min0p3"));
            trainTuple().L1_LooseIsoEG24er2p1_IsoTau27er2p1_dR_Min0p3.push_back(boolean_map.at("L1_LooseIsoEG24er2p1_IsoTau27er2p1_dR_Min0p3"));
            trainTuple().L1_LooseIsoEG22er2p1_Tau70er2p1_dR_Min0p3.push_back(boolean_map.at("L1_LooseIsoEG22er2p1_Tau70er2p1_dR_Min0p3"));
            trainTuple().L1_SingleTau120er2p1.push_back(boolean_map.at("L1_SingleTau120er2p1"));
            trainTuple().L1_SingleTau130er2p1.push_back(boolean_map.at("L1_SingleTau130er2p1"));
            trainTuple().L1_DoubleTau70er2p1.push_back(boolean_map.at("L1_DoubleTau70er2p1"));
            trainTuple().L1_DoubleIsoTau28er2p1.push_back(boolean_map.at("L1_DoubleIsoTau28er2p1"));
            trainTuple().L1_DoubleIsoTau30er2p1.push_back(boolean_map.at("L1_DoubleIsoTau30er2p1"));
            trainTuple().L1_DoubleIsoTau32er2p1.push_back(boolean_map.at("L1_DoubleIsoTau32er2p1"));
            trainTuple().L1_DoubleIsoTau34er2p1.push_back(boolean_map.at("L1_DoubleIsoTau34er2p1"));
            trainTuple().L1_DoubleIsoTau36er2p1.push_back(boolean_map.at("L1_DoubleIsoTau36er2p1"));
            trainTuple().L1_DoubleIsoTau28er2p1_Mass_Max90.push_back(boolean_map.at("L1_DoubleIsoTau28er2p1_Mass_Max90"));
            trainTuple().L1_DoubleIsoTau28er2p1_Mass_Max80.push_back(boolean_map.at("L1_DoubleIsoTau28er2p1_Mass_Max80"));
            trainTuple().L1_DoubleIsoTau30er2p1_Mass_Max90.push_back(boolean_map.at("L1_DoubleIsoTau30er2p1_Mass_Max90"));
            trainTuple().L1_DoubleIsoTau30er2p1_Mass_Max80.push_back(boolean_map.at("L1_DoubleIsoTau30er2p1_Mass_Max80"));
            trainTuple().L1_Mu18er2p1_Tau24er2p1.push_back(boolean_map.at("L1_Mu18er2p1_Tau24er2p1"));
            trainTuple().L1_Mu18er2p1_Tau26er2p1.push_back(boolean_map.at("L1_Mu18er2p1_Tau26er2p1"));
            trainTuple().L1_Mu22er2p1_IsoTau28er2p1.push_back(boolean_map.at("L1_Mu22er2p1_IsoTau28er2p1"));
            trainTuple().L1_Mu22er2p1_IsoTau30er2p1.push_back(boolean_map.at("L1_Mu22er2p1_IsoTau30er2p1"));
            trainTuple().L1_Mu22er2p1_IsoTau32er2p1.push_back(boolean_map.at("L1_Mu22er2p1_IsoTau32er2p1"));
            trainTuple().L1_Mu22er2p1_IsoTau34er2p1.push_back(boolean_map.at("L1_Mu22er2p1_IsoTau34er2p1"));
            trainTuple().L1_Mu22er2p1_IsoTau36er2p1.push_back(boolean_map.at("L1_Mu22er2p1_IsoTau36er2p1"));
            trainTuple().L1_Mu22er2p1_IsoTau40er2p1.push_back(boolean_map.at("L1_Mu22er2p1_IsoTau40er2p1"));
            trainTuple().L1_Mu22er2p1_Tau70er2p1.push_back(boolean_map.at("L1_Mu22er2p1_Tau70er2p1"));
            trainTuple().L1_IsoTau40er2p1_ETMHF80.push_back(boolean_map.at("L1_IsoTau40er2p1_ETMHF80"));
            trainTuple().L1_IsoTau40er2p1_ETMHF90.push_back(boolean_map.at("L1_IsoTau40er2p1_ETMHF90"));
            trainTuple().L1_IsoTau40er2p1_ETMHF100.push_back(boolean_map.at("L1_IsoTau40er2p1_ETMHF100"));
            trainTuple().L1_IsoTau40er2p1_ETMHF110.push_back(boolean_map.at("L1_IsoTau40er2p1_ETMHF110"));
            trainTuple().L1_QuadJet36er2p5_IsoTau52er2p1.push_back(boolean_map.at("L1_QuadJet36er2p5_IsoTau52er2p1"));
            trainTuple().L1_DoubleJet35_Mass_Min450_IsoTau45_RmOvlp.push_back(boolean_map.at("L1_DoubleJet35_Mass_Min450_IsoTau45_RmOvlp"));
            trainTuple().L1_DoubleJet_80_30_Mass_Min420_IsoTau40_RmOvlp.push_back(boolean_map.at("L1_DoubleJet_80_30_Mass_Min420_IsoTau40_RmOvlp"));
        }
    }

    template<typename T>
    void FillVar(const std::string& var_name, T var, const std::string& prefix){
        trainTuple.get<std::vector<T>>(prefix+"_"+var_name).push_back(var);
    }
    int FindVertexIndex(const reco::VertexCollection& vertices, const reco::Track& track){
      const reco::TrackBase *track_to_compare = &track;
      for(size_t n = 0; n< vertices.size() ; ++n){
        for(auto k = vertices.at(n).tracks_begin(); k != vertices.at(n).tracks_end(); ++k){
            if(&**k==track_to_compare) return n;
        }
      }
      return -1;
    }
    void FillTracks(const reco::TrackCollection& Tracks, const std::string prefix, const reco::VertexCollection& vertices)
    {
      const auto push_back= [&](const std::string& var_name, auto var){
        FillVar(var_name, var, prefix);
      };

        for(unsigned n = 0; n < Tracks.size(); ++n){
            push_back("pt", static_cast<Float_t>(Tracks.at(n).pt()));
            push_back("eta", static_cast<Float_t>(Tracks.at(n).eta()));
            push_back("phi", static_cast<Float_t>(Tracks.at(n).phi()));
            push_back("chi2", static_cast<Float_t>(Tracks.at(n).chi2()));
            push_back("ndof", static_cast<Int_t>(Tracks.at(n).ndof()));
            push_back("charge", static_cast<Int_t>(Tracks.at(n).charge()));
            push_back("quality", static_cast<UInt_t>(Tracks.at(n).qualityMask()));
            push_back("dxy", static_cast<Float_t>(Tracks.at(n).dxy()));
            push_back("dz", static_cast<Float_t>(Tracks.at(n).dz()));
            push_back("vertex_id", FindVertexIndex(vertices, Tracks.at(n)));
        }
    }

    void FillVertices(const reco::VertexCollection& vertices, const std::string prefix)
    {
      const auto push_back= [&](const std::string& var_name, auto var){
        FillVar(var_name, var, prefix);
      };
        for(unsigned n = 0; n < vertices.size(); ++n){
            push_back("z", static_cast<Float_t>(vertices.at(n).z()));
            push_back("chi2", static_cast<Float_t>(vertices.at(n).chi2()));
            push_back("ndof", static_cast<Int_t>(vertices.at(n).ndof()));
            Float_t weight = 1/(static_cast<Float_t>(vertices.at(n).zError()));
            push_back("weight", weight);
            Float_t ptv2 = pow(static_cast<Float_t>(vertices.at(n).p4().pt()),2);
            push_back("ptv2", ptv2);

            /* when we will switch completely to SoA tracks
            push_back("weight", vertices.at(n).weight());
            push_back("ptv2", vertices.at(n).ptv2()); */

        }
    }

    void FillCaloTowers(const CaloTowerCollection& caloTowers)
    {
        for(auto iter = caloTowers.begin(); iter != caloTowers.end(); ++iter){
           trainTuple().caloTower_pt.push_back(iter->polarP4().pt());
           trainTuple().caloTower_eta.push_back(iter->polarP4().eta());
           trainTuple().caloTower_phi.push_back(iter->polarP4().phi());
           trainTuple().caloTower_energy.push_back(iter->polarP4().energy());
           trainTuple().caloTower_emEnergy.push_back(iter->emEnergy());
           trainTuple().caloTower_hadEnergy.push_back(iter->hadEnergy());
           trainTuple().caloTower_outerEnergy.push_back(iter->outerEnergy());
           trainTuple().caloTower_emPosition_x.push_back(iter->emPosition().x());
           trainTuple().caloTower_emPosition_y.push_back(iter->emPosition().y());
           trainTuple().caloTower_emPosition_z.push_back(iter->emPosition().z());
           trainTuple().caloTower_hadPosition_x.push_back(iter->hadPosition().x());
           trainTuple().caloTower_hadPosition_y.push_back(iter->hadPosition().y());
           trainTuple().caloTower_hadPosition_z.push_back(iter->hadPosition().z());
           trainTuple().caloTower_hadEnergyHeOuterLayer.push_back(iter->hadEnergyHeOuterLayer());
           trainTuple().caloTower_hadEnergyHeInnerLayer.push_back(iter->hadEnergyHeInnerLayer());
           trainTuple().caloTower_energyInHB.push_back(iter->energyInHB());
           trainTuple().caloTower_energyInHE.push_back(iter->energyInHE());
           trainTuple().caloTower_energyInHF.push_back(iter->energyInHF());
           trainTuple().caloTower_energyInHO.push_back(iter->energyInHO());
           trainTuple().caloTower_numBadEcalCells.push_back(iter->numBadEcalCells());
           trainTuple().caloTower_numRecoveredEcalCells.push_back(iter->numRecoveredEcalCells());
           trainTuple().caloTower_numProblematicEcalCells.push_back(iter->numProblematicEcalCells());
           trainTuple().caloTower_numBadHcalCells.push_back(iter->numBadHcalCells());
           trainTuple().caloTower_numRecoveredHcalCells.push_back(iter->numRecoveredHcalCells());
           trainTuple().caloTower_numProblematicHcalCells.push_back(iter->numProblematicHcalCells());
           trainTuple().caloTower_ecalTime.push_back(iter->ecalTime());
           trainTuple().caloTower_hcalTime.push_back(iter->hcalTime());
           trainTuple().caloTower_hottestCellE.push_back(iter->hottestCellE());
           trainTuple().caloTower_emLvl1.push_back(iter->emLvl1());
           trainTuple().caloTower_hadLv11.push_back(iter->hadLv11());
           trainTuple().caloTower_numCrystals.push_back(iter->numCrystals());
        }
    }

    void FillCaloTaus(const reco::CaloJetCollection& caloTaus)
    {
        for(const auto& caloTau : caloTaus){
            trainTuple().caloTau_pt.push_back(caloTau.p4().pt());
            trainTuple().caloTau_eta.push_back(caloTau.p4().eta());
            trainTuple().caloTau_phi.push_back(caloTau.p4().phi());
            trainTuple().caloTau_energy.push_back(caloTau.p4().energy());
            trainTuple().caloTau_maxEInEmTowers.push_back(caloTau.maxEInEmTowers());
            trainTuple().caloTau_maxEInHadTowers.push_back(caloTau.maxEInHadTowers());
            trainTuple().caloTau_energyFractionHadronic.push_back(caloTau.energyFractionHadronic());
            trainTuple().caloTau_emEnergyFraction.push_back(caloTau.emEnergyFraction());
            trainTuple().caloTau_hadEnergyInHB.push_back(caloTau.hadEnergyInHB());
            trainTuple().caloTau_hadEnergyInHO.push_back(caloTau.hadEnergyInHO());
            trainTuple().caloTau_hadEnergyInHE.push_back(caloTau.hadEnergyInHE());
            trainTuple().caloTau_hadEnergyInHF.push_back(caloTau.hadEnergyInHF());
            trainTuple().caloTau_emEnergyInEB.push_back(caloTau.emEnergyInEB());
            trainTuple().caloTau_emEnergyInEE.push_back(caloTau.emEnergyInEE());
            trainTuple().caloTau_emEnergyInHF.push_back(caloTau.emEnergyInHF());
            trainTuple().caloTau_towersArea.push_back(caloTau.towersArea());
            trainTuple().caloTau_n90.push_back(caloTau.n90());
            trainTuple().caloTau_n60.push_back(caloTau.n60());
        }
    }

    template<typename C>
    void FillCommonParts(C calopart, const caloRecHitCollections& caloRecHits, std::string prefix){
      const auto& position = caloRecHits.Geometry->getGeometry(calopart.id())->getPosition();
      trainTuple.get<std::vector<Float_t>>(prefix+"_rho").push_back(position.perp());
      trainTuple.get<std::vector<Float_t>>(prefix+"_eta").push_back(position.eta());
      trainTuple.get<std::vector<Float_t>>(prefix+"_phi").push_back(position.phi());
      trainTuple.get<std::vector<Float_t>>(prefix+"_energy").push_back(calopart.energy());
      trainTuple.get<std::vector<Float_t>>(prefix+"_time").push_back(calopart.time());
      trainTuple.get<std::vector<ULong64_t>>(prefix+"_detId").push_back(static_cast<ULong64_t>(calopart.id().rawId()));
    }
    template<typename C>
    void FillEcalParts(C calopart, std::string prefix){
        trainTuple.get<std::vector<uint32_t>>(prefix+"_flagsBits").push_back(static_cast<uint32_t>(calopart.flagsBits()));
        trainTuple.get<std::vector<Float_t>>(prefix+"_energyError").push_back(calopart.energyError());
        trainTuple.get<std::vector<Float_t>>(prefix+"_timeError").push_back(calopart.timeError());
        trainTuple.get<std::vector<Float_t>>(prefix+"_chi2").push_back(calopart.chi2());
        trainTuple.get<std::vector<Bool_t>>(prefix+"_isRecovered").push_back(static_cast<Bool_t>(calopart.isRecovered()));
        trainTuple.get<std::vector<Bool_t>>(prefix+"_isTimeValid").push_back(static_cast<Bool_t>(calopart.isTimeValid()));
        trainTuple.get<std::vector<Bool_t>>(prefix+"_isTimeErrorValid").push_back(static_cast<Bool_t>(calopart.isTimeErrorValid()));
      }
    template<typename C>
    void FillHcalParts(C calopart, std::string prefix){
      trainTuple.get<std::vector<ULong64_t>>(prefix+"_flags").push_back(static_cast<ULong64_t>(calopart.flags()));
      trainTuple.get<std::vector<ULong64_t>>(prefix+"_aux").push_back(static_cast<ULong64_t>(calopart.aux()));

    }
    template<typename C>
    void FillHFParts(C calopart, std::string prefix){
      trainTuple.get<std::vector<uint32_t>>(prefix+"_auxHF").push_back(static_cast<uint32_t>(calopart.getAuxHF()));
      trainTuple.get<std::vector<Float_t>>(prefix+"_timeFalling").push_back(calopart.timeFalling());
    }
    template<typename C>
    void FillHBHEParts(C calopart, const caloRecHitCollections& caloRecHits, std::string prefix){
      const auto& positionFront = caloRecHits.Geometry->getGeometry(calopart.idFront())->getPosition();
      trainTuple.get<std::vector<Float_t>>(prefix+"_eraw").push_back(calopart.eraw());
      trainTuple.get<std::vector<Float_t>>(prefix+"_eaux").push_back(calopart.eaux());
      trainTuple.get<std::vector<Float_t>>(prefix+"_rho_front").push_back(positionFront.perp());
      trainTuple.get<std::vector<Float_t>>(prefix+"_eta_front").push_back(positionFront.eta());
      trainTuple.get<std::vector<Float_t>>(prefix+"_phi_front").push_back(positionFront.phi());
      trainTuple.get<std::vector<UInt_t>>(prefix+"_auxHBHE").push_back(static_cast<UInt_t>(calopart.auxHBHE()));
      trainTuple.get<std::vector<UInt_t>>(prefix+"_auxPhase1").push_back(static_cast<UInt_t>(calopart.auxPhase1()));
      trainTuple.get<std::vector<UInt_t>>(prefix+"_auxTDC").push_back(static_cast<UInt_t>(calopart.auxTDC()));
      trainTuple.get<std::vector<Bool_t>>(prefix+"_isMerged").push_back(static_cast<Bool_t>(calopart.isMerged()));
      trainTuple.get<std::vector<ULong64_t>>(prefix+"_flags").push_back(static_cast<ULong64_t>(calopart.flags()));
      trainTuple.get<std::vector<Float_t>>(prefix+"_chi2").push_back(calopart.chi2());
      trainTuple.get<std::vector<Float_t>>(prefix+"_timeFalling").push_back(calopart.timeFalling());
    }

    void FillCaloRecHit(const caloRecHitCollections& caloRecHits)
    {

      for(const auto& caloRecHit_ee : *caloRecHits.ee)
      {
        FillCommonParts(caloRecHit_ee, caloRecHits, "caloRecHit_ee");
        FillEcalParts(caloRecHit_ee, "caloRecHit_ee");
      }
      for(const auto& caloRecHit_eb : *caloRecHits.eb)
      {
        FillCommonParts(caloRecHit_eb, caloRecHits, "caloRecHit_eb");
        FillEcalParts(caloRecHit_eb, "caloRecHit_eb");
      }

      for(const auto& caloRecHit_ho : *caloRecHits.ho)
      {
        FillCommonParts(caloRecHit_ho, caloRecHits, "caloRecHit_ho");
        FillHcalParts(caloRecHit_ho, "caloRecHit_ho");
      }

      for(const auto& caloRecHit_hf : *caloRecHits.hf)
      {
        FillCommonParts(caloRecHit_hf, caloRecHits, "caloRecHit_hf");
        FillHcalParts(caloRecHit_hf, "caloRecHit_hf");
        FillHFParts(caloRecHit_hf, "caloRecHit_hf");
      }
      for(const auto& caloRecHit_hbhe : *caloRecHits.hbhe)
      {
        FillCommonParts(caloRecHit_hbhe, caloRecHits, "caloRecHit_hbhe");
        FillHBHEParts(caloRecHit_hbhe, caloRecHits, "caloRecHit_hbhe");
      }

    }

private:
    const bool isMC;
    TauJetBuilderSetup builderSetup;
    edm::EDGetTokenT<GenEventInfoProduct> genEvent_token;
    edm::EDGetTokenT<std::vector<reco::GenParticle>> genParticles_token;
    edm::EDGetTokenT<std::vector<PileupSummaryInfo>> puInfo_token;
    edm::EDGetTokenT<l1t::TauBxCollection> l1Taus_token;
    edm::EDGetTokenT<CaloTowerCollection> caloTowers_token;
    edm::EDGetTokenT<reco::CaloJetCollection> caloTaus_token;
    edm::EDGetTokenT<HBHERecHitCollection> hbhe_token;
    edm::EDGetTokenT<HORecHitCollection> ho_token;
    edm::EDGetTokenT<HFRecHitCollection> hf_token;
    std::vector<edm::InputTag> ecalLabels;
    std::vector<edm::EDGetTokenT<EcalRecHitCollection>> ecal_tokens;
    edm::ESGetToken<CaloGeometry, CaloGeometryRecord> Geometry_token;
    edm::EDGetTokenT<reco::VertexCollection> vertices_token;
    edm::EDGetTokenT<reco::VertexCollection> pataVertices_token;
    edm::EDGetTokenT<reco::TrackCollection> Tracks_token;
    edm::EDGetTokenT<reco::TrackCollection> pataTracks_token;
    std::map<std::string, edm::EDGetTokenT<trigger::TriggerFilterObjectWithRefs>> l1_token_map ;
    TrainTupleProducerData* data;
    train_tuple::TrainTuple& trainTuple;
    tau_tuple::SummaryTuple& summaryTuple;

};

} // namespace tau_analysis

#include "FWCore/Framework/interface/MakerMacros.h"
using TrainTupleProducer = tau_analysis::TrainTupleProducer;
DEFINE_FWK_MODULE(TrainTupleProducer);
