#!/usr/bin/env python3
"""
Deterministic English Corpus & IR Evaluation Generator for CSIR.
Generates:
  1) data/sample_corpus/: 60 rich English plain-text documents (>110,000 words total)
     plus 1 Markdown document, 1 TREC-tagged document, and 1 empty edge-case document.
  2) data/eval_corpus/ + data/eval_queries.txt + data/eval_qrels.txt:
     20 focused documents with 12 queries and graded qrels for Precision@K, Recall@K,
     MRR, MAP@10, and NDCG@10 benchmarking.
"""

import os
import random

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLE_DIR = os.path.join(BASE_DIR, "data", "sample_corpus")
EVAL_DIR = os.path.join(BASE_DIR, "data", "eval_corpus")

TOPICS = [
    (
        "renewable_energy",
        "Renewable Energy and Clean Power Systems",
        [
            "Renewable energy systems reduce global dependence on fossil fuels by harnessing solar power, wind turbines, hydroelectric reservoirs, and geothermal plants.",
            "Modern photovoltaic panels convert sunlight directly into electricity while utility-scale battery storage smooths intermittent grid demand across peak hours.",
            "Offshore wind farms capture consistent coastal airflows, delivering gigawatts of carbon-free energy to densely populated metropolitan corridors.",
            "Geothermal power stations tap deep subterranean heat reservoirs to produce steady baseload generation independent of seasonal weather fluctuations.",
            "Smart microgrids combine distributed rooftop solar arrays with real-time demand response controllers to stabilize regional voltage and frequency.",
            "Policy incentives, declining lithium-ion storage costs, and modernized high-voltage transmission lines accelerate the global clean energy transition.",
        ],
        [
            "solar", "wind", "geothermal", "hydroelectric", "photovoltaic", "turbine",
            "microgrid", "inverter", "kilowatt", "megawatt", "sustainability", "decarbonization",
            "storage", "battery", "biomass", "clean", "electricity", "emissions", "grid", "renewable"
        ],
    ),
    (
        "algorithms",
        "Algorithms, Data Structures and Complexity Analysis",
        [
            "Fundamental algorithms and data structures govern the efficiency of indexing, searching, sorting, and graph traversal in systems software.",
            "Hash tables with separate chaining provide expected constant-time lookup, whereas balanced binary search trees guarantee logarithmic worst-case bounds.",
            "Merge sort and quicksort partition large datasets recursively, balancing cache locality against worst-case pivot selection behavior.",
            "Dynamic memory management in C requires disciplined pointer ownership, careful allocation sizing, and deterministic cleanup to prevent memory leaks.",
            "Inverted indexes map vocabulary terms to linked posting lists containing document identifiers, term frequencies, and positional offsets.",
            "Empirical benchmarking complements asymptotic Big-O analysis by revealing constant factors, memory bandwidth bottlenecks, and CPU cache misses.",
        ],
        [
            "algorithm", "complexity", "hashing", "pointer", "linked", "quicksort",
            "mergesort", "binary", "traversal", "recursion", "asymptotic", "throughput",
            "latency", "benchmark", "allocation", "deterministic", "sorting", "indexing", "posting", "bucket"
        ],
    ),
    (
        "operating_systems",
        "Operating Systems, Concurrency and Kernel Design",
        [
            "An operating system kernel arbitrates CPU scheduling, virtual memory translation, file system caching, and hardware interrupt handling.",
            "Preemptive multitasking switches threads across processor cores using priority queues and time-slice quanta to maintain interactive responsiveness.",
            "Virtual memory page tables isolate process address spaces while translation lookaside buffers accelerate logical-to-physical address resolution.",
            "Mutex locks, semaphores, and condition variables synchronize shared state among concurrent worker threads and prevent race conditions or deadlocks.",
            "Buffered file I/O reduces expensive kernel system calls by aggregating sequential read and write operations inside user-space stream buffers.",
            "Journaling filesystems record metadata transactions before committing disk blocks so storage volumes recover cleanly after unexpected power failures.",
        ],
        [
            "kernel", "scheduler", "process", "thread", "virtual", "paging", "mutex",
            "semaphore", "deadlock", "interrupt", "syscall", "filesystem", "inode", "buffer",
            "cache", "concurrency", "preemptive", "allocator", "memory", "descriptor"
        ],
    ),
    (
        "computer_networks",
        "Computer Networks, TCP/IP and Internet Routing",
        [
            "Packet-switched computer networks encapsulate application payloads inside layered headers defined by the TCP/IP and OSI reference models.",
            "Transmission Control Protocol ensures reliable ordered delivery through sequence numbers, cumulative acknowledgments, sliding windows, and congestion control.",
            "Border Gateway Protocol exchanges autonomous system routing tables across backbone routers to maintain global Internet reachability.",
            "Domain Name System resolvers translate human-readable hostnames into IPv4 and IPv6 addresses using hierarchical caching name servers.",
            "Transport Layer Security establishes authenticated encrypted channels using public-key handshakes and symmetric session ciphers.",
            "Content delivery networks replicate static assets across edge points of presence to minimize round-trip propagation latency for end users.",
        ],
        [
            "packet", "routing", "protocol", "tcp", "udp", "bandwidth", "congestion",
            "socket", "firewall", "dns", "gateway", "latency", "ethernet", "switch",
            "subnet", "handshake", "throughput", "backbone", "proxy", "network"
        ],
    ),
    (
        "databases",
        "Relational Databases, Transactions and Query Optimization",
        [
            "Relational database management systems organize structured records into normalized tables enforced by primary keys, foreign keys, and schema constraints.",
            "B-tree and B+tree indexes accelerate range scans and equality predicates by keeping sorted keys in high-fanout disk-resident pages.",
            "ACID transaction processing guarantees atomicity, consistency, isolation, and durability through write-ahead logging and two-phase locking.",
            "Cost-based query optimizers evaluate join orders, index selectivity, and histogram statistics to choose low-cost execution plans.",
            "Columnar analytical stores compress homogeneous attribute vectors to speed up large aggregation queries over billions of rows.",
            "Full-text search engines complement relational databases by maintaining inverted indexes tailored to relevance ranking and natural-language queries.",
        ],
        [
            "database", "transaction", "sql", "btree", "schema", "normalization", "isolation",
            "durability", "optimizer", "join", "predicate", "selectivity", "replica", "sharding",
            "columnar", "logging", "query", "index", "tuple", "storage"
        ],
    ),
    (
        "space_exploration",
        "Space Exploration, Orbital Mechanics and Deep-Space Probes",
        [
            "Robotic spacecraft navigate interplanetary trajectories using gravity-assist flybys, precise reaction-wheel attitude control, and deep-space radio telemetry.",
            "Orbital telescopes positioned beyond Earth's atmosphere capture ultraviolet and infrared spectra unblurred by terrestrial air turbulence.",
            "Reusable launch vehicles perform propulsive vertical landings to lower payload launch costs into low Earth orbit and geostationary transfer orbits.",
            "Planetary rovers analyze Martian regolith samples with onboard laser spectrometers to investigate ancient hydrological history.",
            "Deep-space communication arrays decode faint microwave signals transmitted across hundreds of millions of kilometers using error-correcting codes.",
            "Radiation shielding, closed-loop life support, and autonomous navigation remain critical engineering challenges for crewed lunar and Martian missions.",
        ],
        [
            "spacecraft", "orbital", "telescope", "telemetry", "propulsion", "rover",
            "trajectory", "gravity", "satellite", "astronomy", "lunar", "martian",
            "payload", "infrared", "spectrometer", "vacuum", "thruster", "constellation", "mission", "rocket"
        ],
    ),
    (
        "climate_science",
        "Climate Science, Ocean Circulation and Atmospheric Modeling",
        [
            "Global climate models simulate radiative energy balance, atmospheric circulation, cloud microphysics, and ocean heat uptake on coupled spatial grids.",
            "Ice cores drilled in Antarctica preserve trapped air bubbles that record carbon dioxide and methane concentrations across hundreds of millennia.",
            "Thermohaline ocean currents redistribute thermal energy from equatorial latitudes toward polar basins, strongly shaping regional precipitation regimes.",
            "Satellite altimetry and gravimetry track glacier mass balance, sea-level rise, and seasonal Arctic sea-ice extent with millimeter precision.",
            "Reforestation, wetland restoration, and soil carbon sequestration provide nature-based sinks that complement industrial greenhouse gas reductions.",
            "Urban heat island mitigation relies on reflective pavements, shaded tree canopies, and permeable drainage surfaces to protect vulnerable communities.",
        ],
        [
            "climate", "atmosphere", "carbon", "methane", "glacier", "ocean",
            "precipitation", "radiative", "temperature", "biodiversity", "ecosystem", "wetland",
            "sequestration", "albedo", "drought", "monsoon", "arctic", "warming", "aerosol", "modeling"
        ],
    ),
    (
        "urban_transport",
        "Urban Transport, Public Transit and Smart Mobility",
        [
            "High-capacity public transport networks including metro rail, bus rapid transit, and light rail reduce traffic congestion in dense cities.",
            "Dedicated cycling lanes and pedestrian-first streetscapes improve last-mile accessibility while lowering urban particulate air pollution.",
            "Adaptive traffic signal controllers coordinate intersection timing using loop detectors to smooth arterial flow and minimize idling delays.",
            "Electrified commuter rail corridors paired with regenerative braking recover kinetic energy during station approaches.",
            "Integrated contactless fare systems unify municipal buses, suburban trains, and ferry services under transparent distance-based pricing.",
            "Transit-oriented urban planning concentrates housing and commercial services near rail hubs to shorten average daily commute distances.",
        ],
        [
            "transport", "transit", "congestion", "commuter", "metro", "rail",
            "pedestrian", "cycling", "intersection", "arterial", "braking", "corridor",
            "mobility", "bus", "station", "traffic", "urban", "schedule", "fare", "freight"
        ],
    ),
    (
        "medicine_genetics",
        "Medicine, Clinical Diagnostics and Genomic Research",
        [
            "Evidence-based clinical diagnostics combine biochemical assays, medical imaging, and longitudinal patient histories to guide targeted therapies.",
            "High-throughput DNA sequencing identifies inherited genetic variants linked to cardiovascular disease, oncology, and rare metabolic disorders.",
            "Antibiotic stewardship programs monitor microbial resistance patterns in hospitals to preserve the effectiveness of frontline antimicrobial drugs.",
            "Preventive vaccination campaigns establish herd immunity and drastically reduce mortality from infectious respiratory and viral pathogens.",
            "Telemedicine platforms expand specialist consultations to rural clinics through secure video triage and remote vital-sign monitoring.",
            "Randomized double-blind clinical trials evaluate dosage safety, therapeutic efficacy, and statistical significance before regulatory approval.",
        ],
        [
            "clinical", "genomics", "sequencing", "diagnostics", "therapy", "vaccine",
            "antibiotic", "pathogen", "cardiology", "oncology", "biomarker", "pathology",
            "immunology", "dosage", "trial", "hospital", "patient", "epidemiology", "cellular", "protein"
        ],
    ),
    (
        "cybersecurity",
        "Cybersecurity, Cryptography and Secure Systems Engineering",
        [
            "Defense-in-depth security architectures combine network segmentation, least-privilege access control, cryptographic authentication, and continuous auditing.",
            "Memory-safe coding practices and bounds-checked string parsing prevent buffer overflows, format-string bugs, and use-after-free vulnerabilities in C.",
            "Cryptographic hash functions such as SHA-256 verify software integrity, while digital signatures prove message authenticity and non-repudiation.",
            "Multi-factor authentication protects user accounts against credential stuffing and phishing attacks by requiring hardware or time-based tokens.",
            "Intrusion detection systems analyze anomalous packet flows and audit logs to spot lateral movement inside enterprise infrastructures.",
            "Automated fuzz testing and static code analyzers uncover subtle edge-case parser flaws before software releases reach production environments.",
        ],
        [
            "cybersecurity", "cryptography", "encryption", "authentication", "vulnerability", "firewall",
            "signature", "certificate", "intrusion", "auditing", "overflow", "fuzzing",
            "privilege", "phishing", "malware", "integrity", "cipher", "token", "sandbox", "hardening"
        ],
    ),
]

CONNECTORS = [
    "In empirical field studies,",
    "Furthermore, engineering teams observe that",
    "From a systems perspective,",
    "Recent comparative evaluations demonstrate that",
    "In practical deployments,",
    "Consequently, researchers emphasize that",
    "Across multi-year benchmarks,",
    "Under rigorous testing conditions,",
]

EXTRA_CLAUSES = [
    "careful measurement confirms reproducible outcomes across diverse operating workloads.",
    "modular design simplifies maintenance, verification, and long-term technical documentation.",
    "deterministic rules eliminate unpredictable variance during validation and audit reviews.",
    "resource trade-offs between computation speed and memory footprint must be documented quantitatively.",
    "cross-disciplinary collaboration bridges theoretical principles and dependable real-world engineering.",
    "structured error handling protects stability even when encountering malformed or incomplete inputs.",
]


def generate_sample_corpus():
    os.makedirs(SAMPLE_DIR, exist_ok=True)
    rng = random.Random(20261028)
    total_words = 0

    # Generate 60 .txt documents (6 per topic) -> ~1,900 words each -> ~114,000 words total!
    doc_num = 1
    for t_idx, (slug, title, base_sentences, keywords) in enumerate(TOPICS):
        for variant in range(6):
            filename = f"doc_{doc_num:03d}_{slug}.txt"
            filepath = os.path.join(SAMPLE_DIR, filename)

            lines = [
                f"{title} - Study Report #{variant + 1}",
                f"Category: {slug.replace('_', ' ').title()} | Document ID Reference: {doc_num:03d}",
                "",
            ]

            # Cross-pollinate slightly with another topic for realistic multi-topic queries
            secondary_idx = (t_idx + variant + 1) % len(TOPICS)
            sec_slug, _, sec_sentences, sec_keywords = TOPICS[secondary_idx]

            # Build ~18 paragraphs (~105 words each -> ~1,900 words per doc)
            for para in range(18):
                sents = []
                # Pick 4-5 primary sentences + keyword-rich analytical sentences
                for _ in range(4):
                    s = rng.choice(base_sentences)
                    sents.append(s)
                k1, k2, k3 = rng.sample(keywords, 3)
                conn = rng.choice(CONNECTORS)
                extra = rng.choice(EXTRA_CLAUSES)
                sents.append(
                    f"{conn} integrating {k1}, {k2}, and {k3} improves reliability because {extra}"
                )
                if para % 6 == 3:
                    sk = rng.choice(sec_keywords)
                    sents.append(
                        f"Intersecting with {sec_slug.replace('_', ' ')}, practitioners also consider {sk}: {rng.choice(sec_sentences)}"
                    )
                paragraph_text = " ".join(sents)
                lines.append(paragraph_text)
                lines.append("")

            content = "\n".join(lines)
            total_words += len(content.split())
            with open(filepath, "w", encoding="utf-8", newline="\n") as f:
                f.write(content)
            doc_num += 1

    # Add 1 Markdown document (tests .md support)
    md_path = os.path.join(SAMPLE_DIR, "doc_061_ir_overview.md")
    md_text = (
        "# Information Retrieval Pipeline Overview\n\n"
        "A classical document search engine executes document loading, tokenization, text normalization, "
        "inverted index construction, query processing, deterministic relevance scoring (Basic TF, TF-IDF, "
        "and Okapi BM25), and ranked presentation.\n\n"
        "Positional posting lists allow exact phrase search such as \"renewable energy\" and \"hash table\" "
        "without re-scanning raw document files on every query.\n"
    )
    with open(md_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(md_text)
    total_words += len(md_text.split())

    # Add 1 TREC-style SGML document (tests .trec support)
    trec_path = os.path.join(SAMPLE_DIR, "doc_062_cranfield_sample.trec")
    trec_text = (
        "<DOC>\n"
        "<DOCNO>CRAN-001</DOCNO>\n"
        "<TITLE>Experimental Investigation of Aerodynamics and Geothermal Heat Transfer</TITLE>\n"
        "<TEXT>\n"
        "An experimental study of wing boundary layer flow and geothermal heat transfer demonstrates "
        "how classical information retrieval collections evaluate precision, recall, and mean reciprocal rank.\n"
        "</TEXT>\n"
        "</DOC>\n"
    )
    with open(trec_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(trec_text)
    total_words += len(trec_text.split())

    # Add 1 empty file (tests edge-case resilience)
    empty_path = os.path.join(SAMPLE_DIR, "doc_063_empty_edge_case.txt")
    with open(empty_path, "w", encoding="utf-8") as f:
        pass

    print(f"Generated sample_corpus: 63 files, ~{total_words:,} words total.")


def generate_eval_corpus():
    os.makedirs(EVAL_DIR, exist_ok=True)

    eval_docs = [
        ("eval_01.txt", "Solar Power and Renewable Energy Transition\nSolar power and renewable energy reduce dependence on fossil fuels. Photovoltaic solar panels and grid batteries make renewable energy affordable."),
        ("eval_02.txt", "Wind Energy and Offshore Turbines\nWind energy and solar energy are vital renewable energy sources. Offshore wind turbines supply clean renewable electricity to coastal grids."),
        ("eval_03.txt", "Geothermal Baseload Power Plants\nGeothermal energy provides continuous renewable power from underground heat reservoirs without fossil fuel combustion."),
        ("eval_04.txt", "Hash Tables and Inverted Indexes in C\nAn inverted index built on a hash table with linked posting lists enables fast constant-time term lookup in C search engines."),
        ("eval_05.txt", "Sorting Algorithms and Binary Search\nQuicksort, mergesort, and binary search over sorted arrays provide deterministic performance for algorithmic indexing and retrieval."),
        ("eval_06.txt", "Dynamic Memory and Pointers in C\nProper malloc, realloc, and free usage prevents memory leaks and invalid pointer dereferences in modular C applications."),
        ("eval_07.txt", "Operating System Kernel and Virtual Memory\nThe operating system kernel manages virtual memory page tables, CPU process scheduling, and hardware interrupts."),
        ("eval_08.txt", "Concurrency with Mutexes and Threads\nMultithreaded programs synchronize shared data structures using mutex locks and semaphores to prevent race conditions and deadlocks."),
        ("eval_09.txt", "TCP/IP Networking and Congestion Control\nTCP guarantees reliable packet delivery across computer networks through acknowledgments, sliding windows, and congestion control."),
        ("eval_10.txt", "DNS Resolution and Internet Routing\nBGP routing tables and hierarchical DNS servers direct Internet traffic packets reliably across autonomous networks."),
        ("eval_11.txt", "Relational Databases and ACID Transactions\nRelational databases use B-tree indexes, write-ahead logging, and ACID transactions to protect data consistency and durability."),
        ("eval_12.txt", "Query Optimization and Full-Text Indexing\nDatabase query optimizers estimate index selectivity, while inverted indexes support BM25 and TF-IDF text ranking."),
        ("eval_13.txt", "Mars Rovers and Deep-Space Telemetry\nPlanetary rovers analyze Martian geology and transmit radio telemetry back to Earth using deep-space communication antennas."),
        ("eval_14.txt", "Orbital Telescopes and Reusable Rockets\nSpace telescopes observe infrared galaxies from orbit while reusable rockets lower satellite launch costs."),
        ("eval_15.txt", "Climate Modeling and Carbon Sequestration\nAtmospheric climate models track carbon dioxide emissions, ocean heat uptake, and polar glacier melting rates."),
        ("eval_16.txt", "Public Transport and Urban Congestion\nPublic transport networks such as metro rail and electric buses reduce urban traffic congestion and particulate emissions."),
        ("eval_17.txt", "Smart Traffic Signals and Cycling Lanes\nAdaptive traffic controllers and protected cycling corridors improve commuter safety and urban mobility."),
        ("eval_18.txt", "Genomic Sequencing and Clinical Diagnostics\nHigh-throughput DNA sequencing identifies genetic variants to guide clinical diagnostics and precision medicine therapies."),
        ("eval_19.txt", "Vaccination and Antibiotic Stewardship\nPreventive vaccines and hospital antibiotic stewardship curb infectious pathogens and antimicrobial resistance."),
        ("eval_20.txt", "Cryptography and Memory-Safe C Security\nCryptographic signatures, multi-factor authentication, and bounds-checked C buffers harden software against cybersecurity vulnerabilities."),
    ]

    for fname, body in eval_docs:
        with open(os.path.join(EVAL_DIR, fname), "w", encoding="utf-8", newline="\n") as f:
            f.write(body + "\n")

    queries = [
        (1, "renewable energy solar wind"),
        (2, "geothermal renewable power"),
        (3, "inverted index hash table"),
        (4, "binary search sorting algorithms"),
        (5, "kernel virtual memory scheduling"),
        (6, "mutex threads concurrency deadlocks"),
        (7, "tcp packet congestion network"),
        (8, "acid transactions database btree"),
        (9, "mars rover space telemetry"),
        (10, "public transport traffic congestion"),
        (11, "dna sequencing clinical diagnostics"),
        (12, "cryptography cybersecurity vulnerabilities"),
    ]

    qrels = [
        # qid, filename, graded relevance (2=highly relevant, 1=partially relevant)
        (1, "eval_01.txt", 2), (1, "eval_02.txt", 2), (1, "eval_03.txt", 1),
        (2, "eval_03.txt", 2), (2, "eval_01.txt", 1), (2, "eval_02.txt", 1),
        (3, "eval_04.txt", 2), (3, "eval_12.txt", 1), (3, "eval_05.txt", 1),
        (4, "eval_05.txt", 2), (4, "eval_04.txt", 1),
        (5, "eval_07.txt", 2), (5, "eval_08.txt", 1),
        (6, "eval_08.txt", 2), (6, "eval_07.txt", 1),
        (7, "eval_09.txt", 2), (7, "eval_10.txt", 1),
        (8, "eval_11.txt", 2), (8, "eval_12.txt", 1),
        (9, "eval_13.txt", 2), (9, "eval_14.txt", 1),
        (10, "eval_16.txt", 2), (10, "eval_17.txt", 1),
        (11, "eval_18.txt", 2), (11, "eval_19.txt", 1),
        (12, "eval_20.txt", 2), (12, "eval_06.txt", 1),
    ]

    with open(os.path.join(BASE_DIR, "data", "eval_queries.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# qid\tquery\n")
        for qid, q in queries:
            f.write(f"{qid}\t{q}\n")

    with open(os.path.join(BASE_DIR, "data", "eval_qrels.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# qid\tfilename\trelevance\n")
        for qid, fname, rel in qrels:
            f.write(f"{qid}\t{fname}\t{rel}\n")

    print(f"Generated eval_corpus: {len(eval_docs)} docs, {len(queries)} queries, {len(qrels)} qrels.")


if __name__ == "__main__":
    generate_sample_corpus()
    generate_eval_corpus()
