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
}
public static class InputManager {
    static Dictionary<string,InputBinding> bindings;
    internal static void Reset()=>bindings=null;
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
    public static float GetAxis(string action) {
        if(bindings==null)Reload();if(!bindings.TryGetValue(action,out var b))return 0;
        float keyboard=(Key(b.Positive,false)||Key(b.AlternatePositive,false)?1:0)-(Key(b.Negative,false)||Key(b.AlternateNegative,false)?1:0);
        float pad=b.GamepadAxis>=0&&b.GamepadAxis<6?Input.GetGamepadAxis((GamepadAxis)b.GamepadAxis,b.GamepadIndex,Math.Clamp(b.Deadzone,0,.95f)):0;
        if(b.GamepadAxis==1)pad=-pad;
        float value=Math.Abs(keyboard)>Math.Abs(pad)?keyboard:pad;return b.Invert?-value:value;
    }
    public static bool GetButton(string action,bool down=false) {
        if(bindings==null)Reload();if(!bindings.TryGetValue(action,out var b))return false;
        return Key(b.Positive,down)||Key(b.AlternatePositive,down)||(b.GamepadButton>=0&&b.GamepadButton<15&&(down?Input.GetGamepadButtonDown((GamepadButton)b.GamepadButton,b.GamepadIndex):Input.GetGamepadButton((GamepadButton)b.GamepadButton,b.GamepadIndex)));
    }
}
