// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

/** 固定动画动作到原生 Toolset 的映射；不接受调用者指定执行路径。 */
struct FNexusAnimationToolActions final
{
    static const TMap<FString, FString>& Get(bool bWrite)
    {
        static const TMap<FString, FString> Read = {
            {TEXT("Bindings"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_bindings")},
            {TEXT("SequenceTracks"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_sequence")},
            {TEXT("BindingTracks"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_tracks_on_binding")},
            {TEXT("CurrentSequence"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_current_sequence")},
            {TEXT("FocusedSequence"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_focused_sequence")},
            {TEXT("Sections"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_sections")},
            {TEXT("SectionProperties"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_section_properties")},
            {TEXT("SectionRange"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.get_section_range")},
            {TEXT("ChannelNames"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_channel_names")},
            {TEXT("Keys"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_keys")},
            {TEXT("ChannelDefault"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.get_default_value")},
            {TEXT("ControlRigs"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_control_rigs")},
            {TEXT("Controls"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_controls_info")},
            {TEXT("RigTransform"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_transform")},
            {TEXT("RigFloat"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_float")},
            {TEXT("RigBool"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_bool")},
            {TEXT("AnimationLayers"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.get_anim_layers")},
            {TEXT("RigLocalTransform"), TEXT("animation_toolset.toolsets.controlrig.ControlRigTools.get_local_transform")},
            {TEXT("RigGlobalTransform"), TEXT("animation_toolset.toolsets.controlrig.ControlRigTools.get_global_transform")},
        };
        static const TMap<FString, FString> Write = {
            {TEXT("OpenSequence"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.open_sequence")},
            {TEXT("CloseSequence"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.close_sequence")},
            {TEXT("AddSequenceTrack"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_sequence")},
            {TEXT("AddBindingTrack"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.add_track_to_binding")},
            {TEXT("CreateRigTrack"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.find_or_create_track")},
            {TEXT("AddSection"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.add_section")},
            {TEXT("RemoveSection"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.remove_section")},
            {TEXT("SetSectionRange"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.set_section_range")},
            {TEXT("SetSectionEaseIn"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_in")},
            {TEXT("SetSectionEaseOut"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.set_section_ease_out")},
            {TEXT("SetSectionBlendType"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.set_section_blend_type")},
            {TEXT("SetSectionCompletionMode"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.set_section_completion_mode")},
            {TEXT("SetSectionAnimation"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.set_section_animation")},
            {TEXT("BakeTransform"), TEXT("animation_toolset.toolsets.sequencer.SequencerTools.bake_transform")},
            {TEXT("AddFloatKey"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_float")},
            {TEXT("AddBoolKey"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_bool")},
            {TEXT("AddIntegerKey"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_integer")},
            {TEXT("AddStringKey"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.add_key_string")},
            {TEXT("RemoveKey"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.remove_key_at_frame")},
            {TEXT("SetChannelDefault"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.set_default_value")},
            {TEXT("BakeChannelKeys"), TEXT("animation_toolset.toolsets.keyframing.SequencerKeyframingTools.bake_channel_keys")},
            {TEXT("SetRigTransform"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_transform")},
            {TEXT("SetRigEulerTransform"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_euler_transform")},
            {TEXT("SetRigWorldTransform"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_world_transform")},
            {TEXT("SetRigFloat"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_float")},
            {TEXT("SetRigBool"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_bool")},
            {TEXT("SetRigInteger"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.set_int")},
            {TEXT("KeyRigControls"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls")},
            {TEXT("KeyRigControlsAtFrames"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.key_controls_at_frames")},
            {TEXT("BakeToControlRig"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_to_control_rig")},
            {TEXT("CollapseRigLayers"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.collapse_anim_layers")},
            {TEXT("BakeRigSpace"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.bake_space")},
            {TEXT("AddAnimationLayer"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.add_layer_from_selection")},
            {TEXT("DeleteAnimationLayer"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.delete_anim_layer")},
            {TEXT("DuplicateAnimationLayer"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.duplicate_anim_layer")},
            {TEXT("ReorderAnimationLayers"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.reorder_anim_layers")},
            {TEXT("MergeAnimationLayers"), TEXT("animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools.merge_anim_layers")},
            {TEXT("SetRigLocalTransform"), TEXT("animation_toolset.toolsets.controlrig.ControlRigTools.set_local_transform")},
            {TEXT("SetRigGlobalTransform"), TEXT("animation_toolset.toolsets.controlrig.ControlRigTools.set_global_transform")},
            {TEXT("ExportAnimation"), TEXT("animation_toolset.toolsets.import_export.SequencerImportExportTools.export_anim_sequence")},
        };
        return bWrite ? Write : Read;
    }
};
