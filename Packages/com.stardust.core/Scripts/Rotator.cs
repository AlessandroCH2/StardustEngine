using Stardust;
namespace Stardust.Engine;
public class Rotator : ScriptBehaviour {public Vector3 Speed=new Vector3(0,30,0);void OnUpdate(float dt){transform.Rotate(Speed*dt);}}
