# Stardust Feature Lab

Open FeatureLab.sdproject in the editor and build scripts if prompted. The scene demonstrates a second orthographic camera rendering a 256×192 minimap, layer exclusion (the red object does not appear in the map), a sprite, a script-created indexed mesh, water, UI video and parameterized button actions. Space toggles water. The video button pauses/resumes the bundled original color-bar fixture.

Assets/Editor/FeatureLabTools.cs demonstrates a custom inspector and the Tools/Feature Lab editor window. Assets/Scripts/FeatureLabController.cs demonstrates the runtime API. The default core package is installed when the project opens.

The native StardustGpuFeatureTests target renders this scene to build/FeatureLab.png and checks the minimap layer mask. These captures are test artifacts outside the player output folder.
