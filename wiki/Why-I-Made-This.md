### Getting Starting → Why I Made This
***

## A Little About Me 👋

The name's Nick and I'm an [XR Technical Designer](https://www.nmcclure.com) that's spent almost the past 10 years working _intimately_ in Unreal Engine. I'm typically at the crossroads of Engineering, Virtual Production, Design, and Art -- usually tasked with either working at the ground floor of a project to rapidly prototype polished vertical slices, create in-engine tech pre-viz/editor tools and/or operate for a virtual production shoot, or actually design, implement, and ship whole systems and experiences. 

Interaction design, UI/UX design, editor/pipeline tooling, virtual production, software engineering, 3D modeling, systems design, texturing, materials, photogrammetry/LiDAR for digital twins, systems building (I'm a hardware fiend), full-stack prototyping, rendering, lighting, audio & haptic design, spatial interface design, 3D printing, and now almost 3 years of falling down the LLM rabbit hole... and yes, Unreal Engine has almost _**always**_ played a significant role in every one of these disciplines for me.

**I've applied the aforementioned skillsets to:**

- Building and launching [Swing Genius for the Meta Quest 3](https://www.swinggenius.com)
- Overhauling the ears menu and implementing the takes browser for [Unreal Engine 5.3+ Virtual Camera UI](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5.3-release-notes?application_version=5.3#virtualcamera)
- Implementing the front-end for [Unreal Engine's 4.26 Virtual Camera v2](https://www.nmcclure.com/virtual-camera)
- Exploring [future spatial computing ecosystems](https://www.nmcclure.com/empowering-device-ecosystems-with-xr) and [remote desktop systems](https://www.nmcclure.com/spatial-remote-desktop)
- Prototyping and implementing the spatial interfaces and interactions for [Unreal Engine's 4.23 Virtual Scouting toolset](https://www.nmcclure.com/vr-scouting)
- Multi-platform experience prototyping, VProd shoot operating, and final-pixel rendering/editor handoff for Sony & Verizon's [Madison Beer Immersive Reality Experience](https://www.nmcclure.com/madison-beer)
- Working with the Academy Museum to create their first XR experience, [Cyberpunk: A Virtual Production Experience](https://www.youtube.com/watch?v=2k88t8iDbTs)
- Prototyping the spatial interfaces and interaction mechanics for [CNN VR](https://www.nmcclure.com/cnn-vr)

And a variety of other projects that I'm incredibly proud of, but either didn't want to over-encumber this list with or unfortunately am not able to talk about.

As I'm sure you could probably guess by now, I _**love**_ Unreal Engine, and I _**love**_ Blueprint scripting (I really did mean _intimate_). Blueprint scripting has allowed me to pull almost _any idea_ from my mind and make it a reality at a remarkably fast pace, and that's what makes Blueprints so powerful.

## I Wanted to Have My Cake and Eat it Too

Over the many years that I've honed my technical design abilities with both prototyping and building out sprawling system architectures for production-ready applications/experiences using Blueprints, I've come to fully realize a few of their pitfalls:

**Sharing & reviewing is really difficult:**

- I'm having a weird logic bug with one of my functions, and I have a brilliant co-worker that I want to talk it through with that isn't on the project... but I need to either send them multiple screenshots of my blueprint graph, or send them an equally unwieldy blueprint snippet. And with the advent of LLMs, it's essentially a no-go to talk through a Blueprint problem (screenshots are equally problematic there).

**Engineer onboarding mid-project can be quite painful:**

- Having to walk engineers through crucial systems that are all in blueprints (no matter how tidy or well documented) when you're in the middle of a fast-moving project is an uphill battle and can greatly impact resource planning and timelines.

**Navigation difficulty increases drastically with project size:**

- No matter how many reroute nodes I use, wires I keep uncrossed, functions I keep under 20 nodes, interfaces I implement, comments that I add, local variables that I use, or event graphs I keep segmented... blueprints just get cumbersome to navigate as a project scales up.

**Sometimes you just need speed:**

- There are a lot of systems that Blueprints is fine for from a performance perspective, but I've lost count of the number of times I built a system out in blueprints that I optimized as much as humanly possible, and then just had to come to terms with the fact that I just couldn't hit frame time targets without translating all or some parts of it to C++ (sigh).

### All of that to say...

I wanted to have a solution where I could harness the iteration speed of blueprints, find my perf bottlenecks and/or cumbersome navigation areas, quickly swap them out for C++ counterparts, improve upon them, still hit my short turnaround times, and be able to hand off to, or collaborate easier with engineering with minimal fuss...

I made Node to Code because _**I wanted to have my cake and it, too.**_