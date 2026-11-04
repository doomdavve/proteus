# Grodan canal tour

[GH5](../../docs/GOTHENBURG_HARBOUR_PLAN.md#gh5-the-grodan-package): a canal sightseeing tour from
Kungsportsplatsen in the spirit of Gothenburg's Paddan boats, written as a
[tour](../../docs/PLUGIN_API.md#tour-schema). The title offers it as GOTHENBURG HARBOUR,
CONTENT < GRODAN TOUR >, in the Grodan; from the command line:

```sh
./build/wake_velocity --location outer --package proteus.grodan --skin grodan
./build/wake_velocity --plugin plugins/grodan --skin grodan   # live editing
```

The route runs up Vallgraven, west through Stora Hamnkanalen, out at Stora Bommen, up the river to
Hisingsbron, down to Masthugget, and back through Rosenlundskanalen and Vallgraven. Fattighusån,
which the real boats use, is cut off at the map's eastern edge.

The stops are at the sights and at the canal bends. They were placed from the compiled water
(`assets/geometry/outer/water.h`) and the OSM canal centrelines so that the straight line from each
stop to the next keeps its hull clearance: about 60% of the canal's half-width, at most 6 m. A
stop may sit in water as narrow as the Rosenlund narrows (11 m) because the boat recovers there
facing along the canal. `boat_plugin_session.grodan_tour` drives the whole loop in the Grodan
along those lines without the hull leaving the water. When you move a stop, keep the leg to its
neighbours clear of the quays, and run that case.

Names and captions are ASCII (FESKEKORKA, GOTA ALV) until the fonts gain Å, Ä and Ö (GH4b).
