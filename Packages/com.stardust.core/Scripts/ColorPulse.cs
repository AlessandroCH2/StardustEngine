using Stardust;
namespace Stardust.Engine;
public class ColorPulse : ScriptBehaviour {public float Speed=1;void OnUpdate(float dt){var mesh=GetComponent<MeshRenderer>();if(mesh!=null)mesh.color=new Color(.6f+.35f*Mathf.Sin(Time.time*Speed),.5f,.25f);}}
