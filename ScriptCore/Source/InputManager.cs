using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Nodes;
using Stardust.Interop;
namespace Stardust;
public enum GamepadButton { South,East,West,North,LeftBumper,RightBumper,Back,Start,Guide,LeftStick,RightStick,DpadUp,DpadRight,DpadDown,DpadLeft }
public enum GamepadAxis { LeftX,LeftY,RightX,RightY,LeftTrigger,RightTrigger }
public static unsafe partial class Input {
    static JsonNode Pad(int index)=>JsonNode.Parse(Native.FromUtf8(Native.Api.GetComponentJson((ulong)index,100)));
    public static bool IsGamepadConnected(int index=0)=>Pad(index)?["connected"]?.GetValue<bool>()??false;
    public static float GetGamepadAxis(GamepadAxis axis,int index=0,float deadzone=.15f) {if(index<0||index>15||!IsGamepadConnected(index))return 0;deadzone=Math.Clamp(deadzone,0,.95f);float value=Pad(index)?["axes"]?[(int)axis]?.GetValue<float>()??0;if((int)axis>=4)value=(value+1)*.5f;return Math.Abs(value)<deadzone?0:Math.Sign(value)*(Math.Abs(value)-deadzone)/(1-deadzone);}
    public static bool GetGamepadButton(GamepadButton button,int index=0)=>index>=0&&index<16&&(Pad(index)?["buttons"]?[(int)button]?.GetValue<bool>()??false);
    public static bool GetGamepadButtonDown(GamepadButton button,int index=0)=>index>=0&&index<16&&(Pad(index)?["pressed"]?[(int)button]?.GetValue<bool>()??false);
    public static bool GetButton(string action)=>InputManager.GetButton(action);
    public static bool GetButtonDown(string action)=>InputManager.GetButton(action,true);
}
public sealed class InputBinding {
    public KeyCode Positive {get;set;}
    public KeyCode Negative {get;set;}
    public KeyCode AlternatePositive {get;set;}
    public KeyCode AlternateNegative {get;set;}
    public int GamepadAxis {get;set;}=-1;
    public int GamepadButton {get;set;}=-1;
    public int GamepadIndex {get;set;}
    public float Deadzone {get;set;}=.15f;
    public bool Invert {get;set;}
    /// <summary>Units per second the keyboard value moves towards the pressed direction.</summary>
    public float Sensitivity {get;set;}=3;
    /// <summary>Units per second the keyboard value falls back to 0 once released.</summary>
    public float Gravity {get;set;}=3;
    /// <summary>Pressing the opposite direction jumps through 0 instead of sliding.</summary>
    public bool Snap {get;set;}=true;
}
public static class InputManager {
    static Dictionary<string,InputBinding> bindings;
    internal static void Reset(){bindings=null;smoothed.Clear();}
    sealed class AxisState{public float Value;public float Time=-1;}
    static readonly Dictionary<string,AxisState> smoothed=new(StringComparer.OrdinalIgnoreCase);
    public static void Reload() {
        bindings=new(StringComparer.OrdinalIgnoreCase){
            ["Horizontal"]=new(){Positive=KeyCode.D,Negative=KeyCode.A,AlternatePositive=KeyCode.RightArrow,AlternateNegative=KeyCode.LeftArrow,GamepadAxis=0},
            ["Vertical"]=new(){Positive=KeyCode.W,Negative=KeyCode.S,AlternatePositive=KeyCode.UpArrow,AlternateNegative=KeyCode.DownArrow,GamepadAxis=1,Invert=false},
            ["Jump"]=new(){Positive=KeyCode.Space,GamepadButton=0},["Submit"]=new(){Positive=KeyCode.Enter,GamepadButton=0},["Cancel"]=new(){Positive=KeyCode.Escape,GamepadButton=1}};
        if(Assets.Exists("Settings/Input.json")) {
            try {var custom=JsonSerializer.Deserialize<Dictionary<string,InputBinding>>(Assets.ReadAllText("Settings/Input.json"));if(custom!=null){bindings=new(StringComparer.OrdinalIgnoreCase);foreach(var pair in custom)if(pair.Value!=null)bindings[pair.Key]=pair.Value;}}
            catch(Exception e){Debug.LogWarning("Input settings: "+e.Message);}
        }
    }
    public static void Bind(string action,InputBinding binding){if(bindings==null)Reload();bindings[action]=binding??throw new ArgumentNullException(nameof(binding));}
    public static IReadOnlyDictionary<string,InputBinding> Bindings {get {if(bindings==null)Reload();return bindings;}}
    static bool Key(KeyCode code,bool down)=>code!=0&&(down?Input.GetKeyDown(code):Input.GetKey(code));
    // Keys ease in and out; a gamepad stick is already analog and is returned as it is
    public static float GetAxis(string action)=>Read(action,true);
    public static float GetAxisRaw(string action)=>Read(action,false);
    static float Read(string action,bool smooth) {
        if(bindings==null)Reload();if(!bindings.TryGetValue(action,out var b))return 0;
        float keyboard=(Key(b.Positive,false)||Key(b.AlternatePositive,false)?1:0)-(Key(b.Negative,false)||Key(b.AlternateNegative,false)?1:0);
        float pad=b.GamepadAxis>=0&&b.GamepadAxis<6?Input.GetGamepadAxis((GamepadAxis)b.GamepadAxis,b.GamepadIndex,Math.Clamp(b.Deadzone,0,.95f)):0;
        if(b.GamepadAxis==1)pad=-pad;
        if(b.Invert){keyboard=-keyboard;pad=-pad;}
        if(!smooth)return Math.Abs(keyboard)>Math.Abs(pad)?keyboard:pad;
        if(!smoothed.TryGetValue(action,out var state))smoothed[action]=state=new AxisState();
        // Advance once per frame, however many scripts read the axis
        if(state.Time!=Time.time){
            float dt=state.Time<0?Time.deltaTime:Math.Clamp(Time.time-state.Time,0,.1f);state.Time=Time.time;
            if(b.Snap&&keyboard!=0&&state.Value!=0&&Math.Sign(keyboard)!=Math.Sign(state.Value))state.Value=0;
            float target=keyboard,rate=keyboard!=0?b.Sensitivity:b.Gravity;
            state.Value=rate<=0?target:state.Value<target?Math.Min(target,state.Value+rate*dt):Math.Max(target,state.Value-rate*dt);
        }
        return Math.Abs(state.Value)>=Math.Abs(pad)?state.Value:pad;
    }
    public static bool GetButton(string action,bool down=false) {
        if(bindings==null)Reload();if(!bindings.TryGetValue(action,out var b))return false;
        return Key(b.Positive,down)||Key(b.AlternatePositive,down)||(b.GamepadButton>=0&&b.GamepadButton<15&&(down?Input.GetGamepadButtonDown((GamepadButton)b.GamepadButton,b.GamepadIndex):Input.GetGamepadButton((GamepadButton)b.GamepadButton,b.GamepadIndex)));
    }
}
