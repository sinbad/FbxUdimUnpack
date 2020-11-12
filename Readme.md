# FBX UDIM Unpacker

## TL;DR:

This tool takes a UDIM-enabled model in FBX format makes it cheap and easy to
use in real-time engines. 

```
UdimUnpack <inputfile.fbx> <outputfile.fbx>
```

Any polygon UVs which reference UDIM tiles trigger the splitting of the material
"Foo" into "Foo_U1001", "Foo_U1002", "Foo_U1010" and so on based on the UDIM tile.
That poly is then changed to reference that split material and the UVs adjusted
back to the 0-1 range.

This means that a real-time engine just needs to have a separate material for
each UDIM tile and can render the model normally, without any complex UDIM 
tile selector shaders, and in the case of Unreal, without enabling virtual texturing.

If that makes no sense, read on. 🙂

## Introduction to UDIMs

Imagine the situation:

1. You need a more texture resolution on a model than a single 4K texture gives you
1. You decide to solve this by using more than one texture, assigned to different parts of the model
1. However really it's still one material conceptually, you just need the texture space

[UDIMs](https://www.fxguide.com/fxfeatured/udim-uv-mapping/) are one answer to this problem, 
originally designed for the VFX industry. They let you use a single material
with texture "tiles" of whatever resolution you want for different areas of the model.
UVs simply extend beyond the 0-1 range to denote the tile (instead of repeating the same texture as normal)


## So Why UDIMs?

The alternative to UDIMs for using multiple textures is splitting your materials 
while modelling, one for each section where you want another texture. But UDIMs 
are more convenient for artists because:

1. Conceptually it's all just one material really, the texture tiles are an implementation detail
1. It's easier to move UV islands around between texture tiles when you don't have to worry about material assignment
1. Tools like [Substance Painter](https://www.substance3d.com/products/substance-painter/) are designed to work better with UDIMs
   * This includes being able to [generate and paint across tiles](https://magazine.substance3d.com/paint-across-uv-tiles-udims-in-substance-painter/), which you **can't** do when they're pre-split into separate materials


Basically it's all about convenience for artists. In real-time systems we don't
need anywhere near as many texture tiles as in VFX work, but as we all know models
and textures are getting more detailed, so it's becoming common on main models
to need more than one 4K texture. To me it made sense to try to support this
workflow if possible. I certainly found a number of forum threads asking about
it in Unreal Engine and Unity.

## Using UDIMs in game engines

The trouble is, game engines, unlike offline renderers, don't really deal with UDIMs. 
The default behaviour is to just repeat the base texture when the UVs go outside the 0-1 range.
There are some ways to deal with them natively (all of which I'd seen suggested
in threads):

1. Write a shader which samples different textures based on UV range
    * This is unnecessarily expensive both in terms of the branching and having to 
      have all the textures bound at once (can be a lot once you add PBR maps)
1. Use Unreal Engine's beta Virtual Texturing UDIM support
    * Again more expensive and beta, and really designed for large areas where you're
      only seeing parts of the tiles at a time (like in VFX, which is designed for closeups)

If like us, you just want to use UDIMs at a low tile count for the pipeline convenience,
neither of these options are attractive. All we really want is the ability to 
use UDIMs to make content authoring easier, then at game time just use a separate
material for each texture tile (2-3 at most probably).

## What this tool does

This tool takes an input FBX file that uses UDIMs by having UVs that are
outside the 0-1 range. For example, if a polygon's UVs are in the range (1-2, 0-1),
it's the UDIM tile 1002 (conceptually one to the right of the main UV space),
wheras UVs in the range (0-1, 0-1) are UDIM tile 1001.

Any polygon UVs which reference UDIM tiles outside 1001 trigger the splitting of 
the material "Foo" into "Foo_U1001", "Foo_U1002", "Foo_U1010" and so on based on 
the UDIM tile. Polys are then changed to reference the material corresponding
to their UDIM tile, and the UVs are adjusted back to the 0-1 range.

The tool then writes the result to another FBX file. If you import that FBX
into a 3D engine, it will have extra materials from the original, depending on
how many UDIM tiles were used. You can then hook up materials to the correct
UDIM texture tiles, one per texture, and use the model like normal.

## Limitations

I've only tested this tool with FBXs from Blender, importing into Unreal Engine 4.
In theory though, it should work with any FBX from any modeller, and the result
should work with any engine. I can't commit to supporting it for all 
variations but if you have any problems, PRs are welcome.

## Building

### Dependencies

The only dependency is the [Autodesk FBX SDK](https://www.autodesk.com/developer-network/platform-technologies/fbx-sdk-2020-1-1) which you'll need to download and install first.

### CMake

You'll need [CMake](https://cmake.org/) to configure & generate project files.
If you installed the FBX SDK in the standard place it should be found automatically.

I've only tested this on Windows so far with VS 2019, it may not work yet on other platforms
but should be able to be tweaked to do so fairly easily. 

