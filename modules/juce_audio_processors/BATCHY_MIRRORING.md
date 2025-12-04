Batchy vendor note
===================

Projucer 7/8 assumes that the `juce_audio_processors` module ships the official
VST3 SDK, LV2 SDK, and PACE PSL extension headers inside
`format_types/{VST3_SDK,LV2_SDK,pluginterfaces,pslextensions}`. The Tracktion
fork bundled with Batchy only provides those trees under
`juce_audio_processors_headless/format_types`, which is enough for the actual
code because the runtime includes the headless helpers.

However, when the module metadata is parsed during `Projucer --resave`, the
missing directories result in hundreds of empty file references being written
into the generated Xcode and Visual Studio projects. VS in particular treats the
missing `.cpp` files as fatal even if they are excluded from the build, which
breaks every Windows configuration immediately after regeneration.

To keep the generated projects buildable, we mirror the headless SDK trees into
`juce_audio_processors/format_types`. These copies are **not** compiled directly
(Projucer still excludes them), but their presence satisfies the IDE file list
and prevents regeneration from inserting broken file references.

Whenever the JUCE submodule is updated, re-sync these directories so they stay
byte-identical with the upstream headless copies:

```bash
rsync -a modules/juce_audio_processors_headless/format_types/VST3_SDK \
      modules/juce_audio_processors/format_types/

rsync -a modules/juce_audio_processors_headless/format_types/LV2_SDK \
      modules/juce_audio_processors/format_types/

rsync -a modules/juce_audio_processors_headless/format_types/pluginterfaces \
      modules/juce_audio_processors/format_types/

rsync -a modules/juce_audio_processors_headless/format_types/pslextensions \
      modules/juce_audio_processors/format_types/
```

Feel free to script this (e.g. `vendor/sync_juce_sdk_mirrors.sh`) if the vendor
drop is refreshed frequently.
