"""Regenerate the small native rendering demonstration project."""
from pathlib import Path
import json,math,shutil
root=Path(__file__).resolve().parents[1];project=root/'Examples/FeatureLab';assets=project/'Assets'
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(data,indent=2),encoding='utf-8')
for source,target in [(root/'engineIcon.png',assets/'Textures/Icon.png'),(root/'Tests/Fixtures/VideoTest.mpg',assets/'Video/Test.mpg')]:
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
write(assets/'Textures/Icon.sprite',{'texture':'Textures/Icon.png','pixelsPerUnit':120,'pivot':[.5,.5]})
write(assets/'Textures/Minimap.rendertexture',{'width':256,'height':192})
write(assets/'Textures/Icon.png.import.json',{'type':2,'srgb':True,'mipmaps':True,'filter':1,'wrap':0,'maxSize':1024})
write(project/'FeatureLab.sdproject',{'name':'Stardust Feature Lab','company':'Stardust','assemblyName':'FeatureLab','version':'1.0','scenes':[{'path':'Scenes/FeatureLab.scene','enabled':True}],'lastOpenedScene':'Scenes/FeatureLab.scene','player':{'width':1600,'height':900,'vsync':True}})
csproj=(root/'NodulusProject/Nodulus.csproj').read_text().replace('<AssemblyName>Nodulus</AssemblyName>','<AssemblyName>FeatureLab</AssemblyName>').replace('Assets/**/*.cs','Assets/**/*.cs;Packages/**/*.cs').replace('..\\build\\bin\\Release','..\\..\\build\\bin\\Release')
(project/'FeatureLab.csproj').write_text(csproj)
counter=0;entities=[]
def entity(name,pos=(0,0,0),scale=(1,1,1),euler=(0,0,0),layer=0,**components):
    global counter;counter+=1
    x,y,z=[math.radians(a)/2 for a in euler];cx,sx=math.cos(x),math.sin(x);cy,sy=math.cos(y),math.sin(y);cz,sz=math.cos(z),math.sin(z)
    q=[cx*cy*cz+sx*sy*sz,sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz]
    value={'uuid':f'{counter:016x}','name':name,'active':True,'layer':layer,'transform':{'position':pos,'rotation':q,'euler':euler,'scale':scale},**components};entities.append(value);return value
def mesh(color,shader='Stardust/Standard'):return {'mesh':'builtin:Cube','embeddedMaterial':{'shader':shader,'color':color,'roughness':.7},'castShadows':True}
camera=entity('Main Camera',(12,12,16),euler=(-31,37,0),camera={'primary':True,'fov':55,'near':.1})
entity('Minimap Camera',(0,18,0),euler=(-90,0,0),camera={'primary':False,'orthographic':True,'orthographicSize':9,'near':.1,'far':100,'cullingMask':1,'targetTexture':'Textures/Minimap.rendertexture','targetWidth':256,'targetHeight':192})
entity('Sun',euler=(-55,-35,0),directionalLight={'intensity':4,'color':[1,.95,.86],'castShadows':True})
entity('Environment',environment={'skyMode':1,'skyColor':[.12,.18,.25,1],'ambient':1,'minimumAmbient':.15,'shadowStrength':.8,'ambientSky':[.2,.25,.3],'ambientHorizon':[.15,.2,.25],'ambientGround':[.1,.12,.15],'fogDensity':0,'bloom':.03,'vignette':.15})
entity('Island',(0,-.5,0),(14,1,12),meshRenderer=mesh([.25,.38,.22,1]))
actor=entity('Rotating Cube',(0,1.5,0),(2,3,2),meshRenderer=mesh([.12,.5,.8,1]))
entity('Layer 3 - hidden on minimap',(4,1,1),(2,2,2),layer=3,meshRenderer=mesh([.9,.1,.16,1]))
entity('Sprite',(3,3,-3),spriteRenderer={'sprite':'Textures/Icon.sprite','color':[.3,.9,.9,1],'pixelsPerUnit':120})
water=entity('Water',(0,-.05,0),(25,1,25),layer=4,meshRenderer={'mesh':'builtin:Plane','material':'Packages/com.stardust.core/Materials/Water.mat','castShadows':False})
canvas=entity('Canvas',layer=5,canvas={'resolution':[1600,900],'scaleWithScreen':True,'renderMode':0})
def rect(name,x,y,w,h,anchor=(0,1),pivot=(0,1),**components):return entity(name,layer=5,parent=canvas['uuid'],rectTransform={'anchorMin':anchor,'anchorMax':anchor,'pivot':pivot,'position':[x,y],'size':[w,h],'scale':[1,1]},**components)
rect('Minimap',-24,-24,320,240,(1,1),(1,1),uiImage={'texture':'Textures/Minimap.rendertexture','color':[1,1,1,1],'raycastTarget':False})
rect('Title',24,-24,950,60,uiText={'text':'Stardust Feature Lab','fontSize':40,'color':[1,1,1,1],'outlineWidth':0,'raycastTarget':False})
status=rect('Status',24,-88,1000,100,uiText={'text':'Press Play to see mesh scripting, water and video.','fontSize':25,'color':[.85,.92,1,1],'outlineWidth':0,'raycastTarget':False})
video=rect('UI Video',24,24,320,240,(0,0),(0,0),videoPlayer={'clip':'Video/Test.mpg','playOnStart':True,'loop':True},uiImage={'color':[1,1,1,1],'preserveAspect':True})
controller=entity('Feature Lab Controller',scripts=[{'class':'FeatureLabController','fields':{'Camera':camera['uuid'],'Actor':actor['uuid'],'Water':water['uuid'],'Status':status['uuid'],'Video':video['uuid']}}])
for label,method,x in [('Toggle Water','ToggleWater()',370),('Play / Pause Video','ToggleVideo()',650)]:
    button=rect(label,x,28,250,55,(0,0),(0,0),uiImage={'color':[.12,.25,.4,1]},uiText={'text':label,'fontSize':24,'color':[1,1,1,1],'alignment':1,'verticalAlignment':1,'raycastTarget':False},uiButton={'transition':1,'fadeDuration':.15,'interactable':True,'actions':[{'target':controller['uuid'],'component':'Script','slot':0,'method':method,'arguments':[],'enabled':True}]})
write(assets/'Scenes/FeatureLab.scene',{'version':3,'name':'Feature Lab','entities':entities})
print(project/'FeatureLab.sdproject')
