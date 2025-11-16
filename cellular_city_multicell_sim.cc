/*
 * cellular_city_multicell_sim.cc
 *
 * Simulação multi-célula (várias estações rádio-base) 4G / “5G” usando ns-3 (LTE).
 * Permite escolher:
 *  - número de células (nEnbs)
 *  - tamanho da área da cidade (areaSize, lado de um quadrado em metros)
 *  - número de UEs (nUes) usando uint32_t (aceita até 500000+ UEs)
 *
 * Exemplo de uso (a partir da pasta build/):
 *
 *   ./ns3.cellular_city_multicell_sim --tech=4g --nUes=200 --nEnbs=7 --areaSize=2000 --simTime=60
 *   ./ns3.cellular_city_multicell_sim --tech=5g --nUes=500000 --nEnbs=120 --areaSize=20000 --simTime=60
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

NS_LOG_COMPONENT_DEFINE("CellularCityMultiCellSim");

int
main(int argc, char *argv[])
{
    // OBS: nUes agora é uint32_t, permitindo até 4 bilhões de UEs (na teoria).
    uint32_t    nUes      = 100;    // número de usuários (UEs)
    uint16_t    nEnbs     = 4;      // número de estações (células)
    double      simTime   = 60.0;   // tempo de simulação (s)
    std::string tech      = "4g";   // "4g" ou "5g"
    bool        verbose   = true;
    double      areaSize  = 2000.0; // lado do quadrado da cidade, em metros

    CommandLine cmd;
    cmd.AddValue("nUes", "Número de UEs (usuários)", nUes);
    cmd.AddValue("nEnbs", "Número de eNodeBs (células)", nEnbs);
    cmd.AddValue("simTime", "Tempo de simulação (s)", simTime);
    cmd.AddValue("tech", "Tecnologia: 4g ou 5g", tech);
    cmd.AddValue("verbose", "Imprimir logs INFO", verbose);
    cmd.AddValue("areaSize", "Tamanho do lado da área da cidade (m)", areaSize);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("CellularCityMultiCellSim", LOG_LEVEL_INFO);
    }

    NS_LOG_INFO("Iniciando simulação multi-célula com "
                << nUes << " UEs, " << nEnbs << " eNodeBs, tech=" << tech
                << ", area=" << areaSize << "m x " << areaSize << "m");

    // -------------------------
    // 1) Helpers LTE + EPC
    // -------------------------
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    if (tech == "4g")
    {
        lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(50)); // RBs
        lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(50));
    }
    else if (tech == "5g")
    {
        lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(100)); // RBs
        lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(100));
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

    PointToPointHelper p2ph;
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
    remoteHostStaticRouting->AddNetworkRouteTo(
        Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    // -------------------------
    // 3) Nós: múltiplos eNodeBs + UEs
    // -------------------------
    NodeContainer enbNodes;
    NodeContainer ueNodes;
    enbNodes.Create(nEnbs);
    ueNodes.Create(nUes);   // aceita uint32_t

    // 3.1 Mobilidade dos eNodeBs: grade sobre a área
    MobilityHelper mobilityEnb;
    mobilityEnb.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityEnb.Install(enbNodes);

    uint16_t nRows = std::floor(std::sqrt(nEnbs));
    if (nRows == 0) nRows = 1;
    uint16_t nCols = std::ceil(static_cast<double>(nEnbs) / nRows);

    double half = areaSize / 2.0;
    double dx = areaSize / (nCols + 1);
    double dy = areaSize / (nRows + 1);

    for (uint16_t i = 0; i < nEnbs; ++i)
    {
        uint16_t row = i / nCols;
        uint16_t col = i % nCols;

        double x = -half + (col + 1) * dx;
        double y = -half + (row + 1) * dy;
        double z = 30.0;

        Ptr<MobilityModel> mm = enbNodes.Get(i)->GetObject<MobilityModel>();
        mm->SetPosition(Vector(x, y, z));
        NS_LOG_INFO("eNodeB " << i << " em (" << x << ", " << y << ", " << z << ")");
    }

    // 3.2 Mobilidade dos UEs (uint32_t loops)
    Ptr<UniformRandomVariable> posX = CreateObject<UniformRandomVariable>();
    posX->SetAttribute("Min", DoubleValue(-half));
    posX->SetAttribute("Max", DoubleValue(half));

    Ptr<UniformRandomVariable> posY = CreateObject<UniformRandomVariable>();
    posY->SetAttribute("Min", DoubleValue(-half));
    posY->SetAttribute("Max", DoubleValue(half));

    Ptr<ListPositionAllocator> uePositionAlloc = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < nUes; ++i)
    {
        double x = posX->GetValue();
        double y = posY->GetValue();
        uePositionAlloc->Add(Vector(x, y, 1.5));
    }

    MobilityHelper mobilityUe;
    mobilityUe.SetPositionAllocator(uePositionAlloc);
    mobilityUe.SetMobilityModel(
        "ns3::RandomWalk2dMobilityModel",
        "Bounds", RectangleValue(Rectangle(-half, half, -half, half)),
        "Speed", StringValue("ns3::UniformRandomVariable[Min=0.5|Max=2.0]"));
    mobilityUe.Install(ueNodes);

    // -------------------------
    // 4) Dispositivos LTE
    // -------------------------
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIfaces =
        epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

    for (uint32_t i = 0; i < nUes; ++i)
    {
        Ptr<Node> ue = ueNodes.Get(i);
        Ptr<Ipv4StaticRouting> ueStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(ue->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    // Associação automática: melhor eNodeB (RSRP)
    lteHelper->Attach(ueDevs);

    // -------------------------
    // 5) Aplicações (tráfego UDP)
    // -------------------------
    uint16_t dlPort = 1234;

    UdpServerHelper udpServer(dlPort);
    ApplicationContainer serverApps = udpServer.Install(remoteHost);
    serverApps.Start(Seconds(0.1));
    serverApps.Stop(Seconds(simTime));

    ApplicationContainer clientApps;
    double   packetInterval = 0.02; // cuidado: 500k UEs com isso é INSANO
    uint32_t packetSize     = 200;

    for (uint32_t i = 0; i < nUes; ++i)
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

    double   totalDelay       = 0.0;
    double   totalJitter      = 0.0;
    uint64_t totalRxPackets   = 0;
    uint64_t totalRxBytes     = 0;
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
        double meanDelay  = totalDelay / (double)totalRxPackets;
        double meanJitter = 0.0;

        if (totalRxPackets > 1)
        {
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

    std::cout << "================ RESULTADOS MULTI-CELULA (" << tech << ") ================" << std::endl;
    std::cout << "Usuarios (UEs):            " << nUes << std::endl;
    std::cout << "eNodeBs (células):         " << nEnbs << std::endl;
    std::cout << "Area da cidade (m):        " << areaSize << " x " << areaSize << std::endl;
    std::cout << "Tempo de simulacao (s):    " << simTime << std::endl;
    std::cout << "Atraso medio (ms):         " << meanDelayMs << std::endl;
    std::cout << "Jitter medio (ms):         " << jitterMs << std::endl;
    std::cout << "Throughput total (Mbps):   " << throughputMbps << std::endl;
    std::cout << "Taxa de perda (%):         " << lossRatePct << std::endl;
    std::cout << "Pacotes recebidos:         " << totalRxPackets << std::endl;
    std::cout << "Pacotes perdidos:          " << totalLostPackets << std::endl;
    std::cout << "================================================================" << std::endl;

    Simulator::Destroy();
    return 0;
}

