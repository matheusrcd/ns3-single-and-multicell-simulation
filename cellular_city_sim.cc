/* 
 * cellular_city_sim.cc
 *
 * Simulação de uma célula 4G/“5G” para comparar atraso, jitter,
 * throughput e perda de pacotes usando NS-3 (módulo LTE).
 *
 * Execute, por exemplo (a partir da pasta build/):
 *
 *   ./ns3.cellular_city_sim --tech=4g --nUes=50 --simTime=30
 *   ./ns3.cellular_city_sim --tech=5g --nUes=50 --simTime=30
 *
 * Depois compare os valores impressos.
 */

#include <iostream>
#include <cmath>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CellularCitySim");

int
main(int argc, char *argv[])
{
    // Parâmetros que você pode ajustar
    uint16_t nUes      = 50;     // número de usuários
    double   simTime   = 30.0;   // duração em segundos
    std::string tech   = "4g";   // "4g" ou "5g"
    bool verbose       = true;

    CommandLine cmd;
    cmd.AddValue("nUes", "Número de UEs (usuários)", nUes);
    cmd.AddValue("simTime", "Tempo de simulação (s)", simTime);
    cmd.AddValue("tech", "Tecnologia: 4g ou 5g", tech);
    cmd.AddValue("verbose", "Imprimir mais logs", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("CellularCitySim", LOG_LEVEL_INFO);
    }

    NS_LOG_INFO("Iniciando simulação com " << nUes
                 << " UEs, tecnologia = " << tech);

    // -------------------------
    // 1) Helpers LTE + EPC
    // -------------------------
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // Ajustes de “perfil” para 4G x 5G
    // (usando o mesmo módulo LTE, mas com parâmetros diferentes)
    if (tech == "4g")
    {
        // Emular 4G: banda menor
        lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(50)); // RBs
        lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(50));
    }
    else if (tech == "5g")
    {
        // Emular 5G: mais banda
        lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(100)); // RBs
        lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(100));
        // Poderia ajustar scheduler, MIMO, etc. aqui
    }
    else
    {
        NS_ABORT_MSG("Valor inválido para --tech (use 4g ou 5g)");
    }

    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // -------------------------
    // 2) Host remoto (Internet)
    // -------------------------
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);

    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    // Link P2P entre PGW e host remoto
    PointToPointHelper p2ph;
    // Aqui podemos usar um atraso menor no “5G” para emular núcleo mais rápido
    if (tech == "4g")
    {
        p2ph.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
        p2ph.SetChannelAttribute("Delay", StringValue("10ms"));
    }
    else // 5g
    {
        p2ph.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
        p2ph.SetChannelAttribute("Delay", StringValue("2ms"));
    }

    NetDeviceContainer internetDevs = p2ph.Install(pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIfaces = ipv4h.Assign(internetDevs);

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    // Rede dos UEs: 7.x.x.x
    remoteHostStaticRouting->AddNetworkRouteTo(
        Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    // -------------------------
    // 3) Nós da célula (eNodeB + UEs)
    // -------------------------
    NodeContainer enbNodes;
    NodeContainer ueNodes;
    enbNodes.Create(1);
    ueNodes.Create(nUes);

    // Mobilidade: eNodeB fixo no centro, UEs andando aleatoriamente
    MobilityHelper mobilityEnb;
    mobilityEnb.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityEnb.Install(enbNodes);
    enbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 30.0));

    MobilityHelper mobilityUe;
    mobilityUe.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=-500.0|Max=500.0]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=-500.0|Max=500.0]"));
    mobilityUe.SetMobilityModel(
        "ns3::RandomWalk2dMobilityModel",
        "Bounds", RectangleValue(Rectangle(-500, 500, -500, 500)),
        "Speed", StringValue("ns3::UniformRandomVariable[Min=0.5|Max=2.0]"));
    mobilityUe.Install(ueNodes);

    // -------------------------
    // 4) Dispositivos LTE
    // -------------------------
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

    // Pilha IP nos UEs
    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIfaces =
        epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

    // Rota default dos UEs -> PGW
    for (uint16_t i = 0; i < nUes; ++i)
    {
        Ptr<Node> ue = ueNodes.Get(i);
        Ptr<Ipv4StaticRouting> ueStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(ue->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    // Todos os UEs conectados ao mesmo eNodeB (célula única)
    for (uint16_t i = 0; i < nUes; ++i)
    {
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(0));
    }

    // -------------------------
    // 5) Aplicações (tráfego)
    // -------------------------
    // Modelamos tráfego sensível a atraso (UDP CBR) dos UEs -> host remoto.
    uint16_t dlPort = 1234;

    // Servidor UDP no host remoto
    UdpServerHelper udpServer(dlPort);
    ApplicationContainer serverApps = udpServer.Install(remoteHost);
    serverApps.Start(Seconds(0.1));
    serverApps.Stop(Seconds(simTime));

    // Clientes UDP em todos os UEs
    ApplicationContainer clientApps;
    double packetInterval = 0.1; // 100 ms -> 10 pacotes/s
    uint32_t packetSize   = 200;  // bytes (aprox. VoIP/game)

    for (uint16_t i = 0; i < nUes; ++i)
    {
        UdpClientHelper udpClient(internetIfaces.GetAddress(1), dlPort);
        udpClient.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
        udpClient.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        udpClient.SetAttribute("PacketSize", UintegerValue(packetSize));

        clientApps.Add(udpClient.Install(ueNodes.Get(i)));
    }

    clientApps.Start(Seconds(0.5));
    clientApps.Stop(Seconds(simTime));

    // -------------------------
    // 6) FlowMonitor (métricas)
    // -------------------------
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // -------------------------
    // 7) Processar resultados
    // -------------------------
    monitor->CheckForLostPackets();
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

    double   totalDelay      = 0.0;
    double   totalJitter     = 0.0;
    uint64_t totalRxPackets  = 0;
    uint64_t totalRxBytes    = 0;
    uint64_t totalLostPackets = 0;

    for (auto const &flow : stats)
    {
        FlowId id = flow.first;
        FlowMonitor::FlowStats fs = flow.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);

        double throughputMbps = (fs.rxBytes * 8.0) / (simTime * 1e6);
        double meanDelayMs = 0.0;
        if (fs.rxPackets > 0)
        {
            meanDelayMs = (fs.delaySum.GetSeconds() / fs.rxPackets) * 1000.0;
        }

        NS_LOG_INFO("Flow " << id
                     << " (" << t.sourceAddress << " -> " << t.destinationAddress << "): "
                     << "Throughput = " << throughputMbps << " Mbps, "
                     << "Atraso médio = " << meanDelayMs << " ms, "
                     << "RxPackets = " << fs.rxPackets << ", "
                     << "LostPackets = " << fs.lostPackets);

        totalDelay      += fs.delaySum.GetSeconds();
        if (fs.rxPackets > 1)
        {
            // jitterSum é a soma das variações de atraso entre pacotes consecutivos
            totalJitter += fs.jitterSum.GetSeconds();
        }
        totalRxPackets   += fs.rxPackets;
        totalRxBytes     += fs.rxBytes;
        totalLostPackets += fs.lostPackets;
    }

    double meanDelayMs    = 0.0;
    double jitterMs       = 0.0;
    double throughputMbps = 0.0;
    double lossRatePct    = 0.0;

    if (totalRxPackets > 0)
    {
        double meanDelay  = totalDelay / (double)totalRxPackets; // s
        double meanJitter = 0.0;

        if (totalRxPackets > 1)
        {
            // Aproximação: jitter médio = jitterSum_total / (N-1)
            meanJitter = totalJitter / (double)(totalRxPackets - 1);
        }

        meanDelayMs    = meanDelay  * 1000.0;
        jitterMs       = meanJitter * 1000.0;
        throughputMbps = (totalRxBytes * 8.0) / (simTime * 1e6);
    }

    uint64_t totalOfferedPackets = totalRxPackets + totalLostPackets;
    if (totalOfferedPackets > 0)
    {
        lossRatePct = (double)totalLostPackets * 100.0 / (double)totalOfferedPackets;
    }

    std::cout << "================ RESULTADOS (" << tech << ") ================" << std::endl;
    std::cout << "Usuarios (UEs):            " << nUes << std::endl;
    std::cout << "Tempo de simulacao (s):    " << simTime << std::endl;
    std::cout << "Atraso medio (ms):         " << meanDelayMs << std::endl;
    std::cout << "Jitter medio (ms):         " << jitterMs << std::endl;
    std::cout << "Throughput total (Mbps):   " << throughputMbps << std::endl;
    std::cout << "Taxa de perda (%):         " << lossRatePct << std::endl;
    std::cout << "Pacotes recebidos:         " << totalRxPackets << std::endl;
    std::cout << "Pacotes perdidos:          " << totalLostPackets << std::endl;
    std::cout << "==================================================" << std::endl;

    Simulator::Destroy();
    return 0;
}

